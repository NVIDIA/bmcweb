// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION &
// AFFILIATES. All rights reserved.

#include "component_integrity.hpp"

#include <cstdint>
#include <vector>

#include <gtest/gtest.h>

namespace redfish
{
namespace
{

TEST(SpdmNonceTest, GeneratesDistinct32ByteNonces)
{
    const auto firstOpt = generateSpdmNonce();
    const auto secondOpt = generateSpdmNonce();

    ASSERT_TRUE(firstOpt.has_value());
    ASSERT_TRUE(secondOpt.has_value());
    const std::vector<uint8_t> first =
        firstOpt.value_or(std::vector<uint8_t>{});
    const std::vector<uint8_t> second =
        secondOpt.value_or(std::vector<uint8_t>{});
    EXPECT_EQ(first.size(), 32U);
    EXPECT_EQ(second.size(), 32U);
    EXPECT_NE(first, second);
}

TEST(SpdmNonceTest, MatchesOnlyTheSentNonce)
{
    const std::vector<uint8_t> sent(32, 0xa5);
    std::vector<uint8_t> other = sent;
    other.back() = 0x5a;

    EXPECT_TRUE(spdmNonceMatches(sent, sent));
    EXPECT_FALSE(spdmNonceMatches(sent, other));
    EXPECT_FALSE(spdmNonceMatches(sent, {}));
}

TEST(SpdmNonceTest, RejectsWrongNonceSize)
{
    const std::vector<uint8_t> shortNonce(31, 0xa5);

    EXPECT_FALSE(spdmNonceMatches(shortNonce, shortNonce));
    EXPECT_FALSE(spdmNonceMatches({}, {}));
}

} // namespace
} // namespace redfish
