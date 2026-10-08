// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION &
// AFFILIATES. All rights reserved.

#include "shmem_utils.hpp"
#include "utils/hex_utils.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <regex>
#include <set>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

namespace redfish::shmem
{
namespace
{

TEST(ShmemUtilsTest, OemReportIsUnprefixedOrHgxPlatformEnvironmentMetrics)
{
    EXPECT_TRUE(isOemPlatformMetricsReport("PlatformEnvironmentMetrics_0"));
    EXPECT_TRUE(isOemPlatformMetricsReport("HGX_PlatformEnvironmentMetrics_0"));
}

TEST(ShmemUtilsTest, OtherReportsAreNotOemReports)
{
    EXPECT_FALSE(
        isOemPlatformMetricsReport("MGX_PlatformEnvironmentMetrics_0"));
    EXPECT_FALSE(isOemPlatformMetricsReport("HGX_ProcessorMetrics_0"));
    EXPECT_FALSE(isOemPlatformMetricsReport("PlatformEnvironmentMetrics_1"));
    EXPECT_FALSE(isOemPlatformMetricsReport("PlatformMetrics"));
    EXPECT_FALSE(isOemPlatformMetricsReport(""));
}

void regexReplaceNumber(const std::string& input, const std::string& key,
                        const std::string& value,
                        std::set<std::string>& replacedNames)
{
    std::regex pattern(key + "(\\d+)");
    std::smatch match;
    if (value == "{BSWild}" || value == "{PDBWild}" || value == "{BFSWild}")
    {
        if (!std::regex_search(input, match, pattern))
        {
            return;
        }
        std::size_t lastSlash = input.find_last_of('/');
        if (lastSlash != std::string::npos)
        {
            replacedNames.insert(input.substr(lastSlash + 1));
        }
        return;
    }
    if (value == "{BF3SWild}")
    {
        if (!std::regex_search(input, match, pattern))
        {
            return;
        }
        std::size_t lastSlash = input.find_last_of('/');
        if (lastSlash == std::string::npos)
        {
            return;
        }
        std::string name = input.substr(lastSlash + 1);
        if (std::regex_search(name, match, pattern))
        {
            replacedNames.insert(key + "{BFWild}" + match.suffix().str());
        }
        return;
    }
    if (std::regex_search(input, match, pattern))
    {
        replacedNames.insert(match[1].str());
    }
}

TEST(ShmemUtils, ReplaceNumberMatchesPreviousRegexBehavior)
{
    using Case = std::tuple<std::string, std::string, std::string>;
    const std::vector<Case> cases = {
        {"/redfish/v1/Systems/system/Processors/CPU_12", "CPU_", "{CWild}"},
        {"CPU_invalid/CPU_9", "CPU_", "{CWild}"},
        {"/xyz/Chassis_0_FAN_8", "Chassis_", "{BSWild}"},
        {"/xyz/BlueField_3_TEMP_1", "BlueField_", "{BFSWild}"},
        {"/xyz/BF3_Slot_2_TEMP_5", "BF3_Slot_", "{BF3SWild}"},
        {"/xyz/GPU_12_TEMP_34", "GPU_\\d+_TEMP_", "{GTWild}"},
        {"/xyz/GPU_invalid_TEMP_34", "GPU_\\d+_TEMP_", "{GTWild}"},
        {"/xyz/no-match", "CPU_", "{CWild}"},
    };

    for (const auto& [input, key, value] : cases)
    {
        std::set<std::string> expected;
        std::set<std::string> actual;
        regexReplaceNumber(input, key, value, expected);
        replaceNumber(input, key, value, actual);
        EXPECT_EQ(actual, expected) << input << ' ' << key << ' ' << value;
    }
}

TEST(ShmemUtils, NetworkAdapterCXMatchesBothLiteralPrefixes)
{
    const MetricsReplacement replacement(networkAdapterCX, "{CNXWild}",
                                         "CNXWild", true);
    const std::vector<std::string> properties = {
        "/redfish/v1/Chassis/CX_3/Sensors/Reading",
        "/redfish/v1/Chassis/CX_NIC_7/Sensors/Reading",
        "/redfish/v1/Chassis/CX_invalid/Sensors/Reading"};
    nlohmann::json wildCards = nlohmann::json::array();

    metricsReplacements(replacement, wildCards, properties);

    ASSERT_EQ(wildCards.size(), 1);
    EXPECT_EQ(wildCards[0]["Name"], "CNXWild");
    EXPECT_EQ(wildCards[0]["Values"], nlohmann::json({"3", "7"}));
}

TEST(ShmemUtils, NumberExtractionMatchesPreviousRegexBehavior)
{
    using Case = std::pair<std::string, std::string>;
    const std::vector<Case> cases = {
        {"/xyz/NVSwitch_10/Ports/NVLink_2", "NVSwitch_"},
        {"CPU_invalid/CPU_7", "CPU_"},
        {"/xyz/UtilizationPercent/13", "UtilizationPercent/"},
        {"/xyz/ConnectX_4", "ConnectX_"},
        {"/xyz/no-match", "CPU_"},
    };

    for (const auto& [input, key] : cases)
    {
        std::set<int64_t> actual;
        insertNumberAfterPattern(input, key, actual);

        std::set<int64_t> expected;
        std::regex pattern(key + "(\\d+)");
        std::smatch match;
        if (std::regex_search(input, match, pattern))
        {
            expected.insert(stringToInt64(match[1].str()).value_or(0));
        }
        EXPECT_EQ(actual, expected) << input << ' ' << key;
    }
}

TEST(ShmemUtils, NetworkAdapterLinkMatchesBothPrefixes)
{
    const std::vector<std::string> properties = {
        "/NIC_1/Ports/Port_2",
        "/NIC_3/Ports/NVLinkManagement_4",
        "Port_invalid/NVLinkManagement_5",
        "NVLinkManagement_6/Port_7",
        "Port_8/NVLinkManagement_9",
        "no-match"};
    const std::regex pattern("(?:Port|NVLinkManagement)_(\\d+)");

    for (const std::string& property : properties)
    {
        std::set<int64_t> actual;
        insertNumberAfterPattern(property, networkAdapterLink, actual);

        std::set<int64_t> expected;
        std::smatch match;
        if (std::regex_search(property, match, pattern))
        {
            expected.insert(stringToInt64(match[1].str()).value_or(0));
        }
        EXPECT_EQ(actual, expected) << property;
    }
}

TEST(ShmemUtils, NvLinkTypeExtractionMatchesPreviousRegexBehavior)
{
    const std::vector<std::string> properties = {
        "/Ports/NVLink_3", "/Ports/InterswitchPort_7",
        "/Ports/NVLinkManagement_11", "NVLink_invalid/NVLink_5", "no-match"};
    const std::regex pattern(
        "(NVLink|InterswitchPort|NVLinkManagement)_(\\d+)");

    for (const std::string& property : properties)
    {
        std::optional<TypeNumberMatch> actual = findNvLinkTypeAndId(property);
        std::smatch match;
        if (!std::regex_search(property, match, pattern))
        {
            EXPECT_FALSE(actual) << property;
            continue;
        }
        if (!actual)
        {
            ADD_FAILURE() << property;
            continue;
        }
        EXPECT_EQ(actual.value().type, match[1].str()) << property;
        EXPECT_EQ(actual.value().id, stringToInt64(match[2].str()).value_or(0))
            << property;
    }
}

TEST(ShmemUtils, PortExtractionMatchesPreviousRegexBehavior)
{
    const std::vector<std::string> properties = {
        "/Ports/Upstream_2", "/Ports/PCIe_Upstream_14/Reading",
        "/Ports/Type_3_suffix", "/Ports/_2", "no-match"};
    const std::regex pattern("/Ports/(\\w+)_(\\d+)");

    for (const std::string& property : properties)
    {
        std::optional<PortMatch> actual = findPort(property);
        std::smatch match;
        if (!std::regex_search(property, match, pattern))
        {
            EXPECT_FALSE(actual) << property;
            continue;
        }
        if (!actual)
        {
            ADD_FAILURE() << property;
            continue;
        }
        EXPECT_EQ(actual.value().type, match[1].str()) << property;
        EXPECT_EQ(actual.value().id, stringToInt64(match[2].str()).value_or(0))
            << property;
    }
}

} // namespace
} // namespace redfish::shmem
