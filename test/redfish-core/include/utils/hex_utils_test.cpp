// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: Copyright OpenBMC Authors
#include "utils/hex_utils.hpp"

#include <cctype>
#include <cstdint>
#include <limits>
#include <optional>
#include <vector>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

namespace
{

using ::testing::IsEmpty;

TEST(BytesToHexString, OnSuccess)
{
    EXPECT_EQ(bytesToHexString({{0x1a, 0x2b}}), "1A2B");
}

TEST(HexCharToNibble, ReturnsCorrectNibbleForEveryHexChar)
{
    for (char c = 0; c < std::numeric_limits<char>::max(); ++c)
    {
        uint8_t expected = 16;
        if (isdigit(c) != 0)
        {
            expected = static_cast<uint8_t>(c) - '0';
        }
        else if (c >= 'A' && c <= 'F')
        {
            expected = static_cast<uint8_t>(c - 'A') + 10U;
        }
        else if (c >= 'a' && c <= 'f')
        {
            expected = static_cast<uint8_t>(c - 'a') + 10U;
        }

        EXPECT_EQ(hexCharToNibble(c), expected);
    }
}

TEST(HexStringToBytes, Success)
{
    std::vector<uint8_t> hexBytes = {0x01, 0x23, 0x45, 0x67,
                                     0x89, 0xAB, 0xCD, 0xEF};
    EXPECT_EQ(hexStringToBytes("0123456789ABCDEF"), hexBytes);
    EXPECT_THAT(hexStringToBytes(""), IsEmpty());
}

TEST(HexStringToBytes, Failure)
{
    EXPECT_THAT(hexStringToBytes("Hello"), IsEmpty());
    EXPECT_THAT(hexStringToBytes("`"), IsEmpty());
    EXPECT_THAT(hexStringToBytes("012"), IsEmpty());
}

TEST(StringToInt64, ParsesValidValues)
{
    EXPECT_EQ(stringToInt64("0"), 0);
    EXPECT_EQ(stringToInt64("12345"), 12345);
    EXPECT_EQ(stringToInt64("-42"), -42);
    EXPECT_EQ(stringToInt64("9223372036854775807"),
              std::numeric_limits<int64_t>::max());
}

TEST(StringToInt64, RejectsInvalidValues)
{
    EXPECT_EQ(stringToInt64(""), std::nullopt);
    EXPECT_EQ(stringToInt64("12abc"), std::nullopt);
    EXPECT_EQ(stringToInt64("abc"), std::nullopt);
    EXPECT_EQ(stringToInt64(" 12"), std::nullopt);
    EXPECT_EQ(stringToInt64("99999999999999999999"), std::nullopt);
}

TEST(StringToUint64, ParsesValidValues)
{
    EXPECT_EQ(stringToUint64("0"), 0U);
    EXPECT_EQ(stringToUint64("12345"), 12345U);
    EXPECT_EQ(stringToUint64("18446744073709551615"),
              std::numeric_limits<uint64_t>::max());
}

TEST(StringToUint64, RejectsInvalidValues)
{
    EXPECT_EQ(stringToUint64(""), std::nullopt);
    EXPECT_EQ(stringToUint64("-1"), std::nullopt);
    EXPECT_EQ(stringToUint64("12xyz"), std::nullopt);
    EXPECT_EQ(stringToUint64("999999999999999999999999"), std::nullopt);
}

TEST(HexStringToInt64, ParsesValidValues)
{
    EXPECT_EQ(hexStringToInt64("ff"), 255);
    EXPECT_EQ(hexStringToInt64("0xff"), 255);
    EXPECT_EQ(hexStringToInt64("0X1A"), 26);
    EXPECT_EQ(hexStringToInt64("7fffffffffffffff"),
              std::numeric_limits<int64_t>::max());
}

TEST(HexStringToInt64, RejectsInvalidValues)
{
    EXPECT_EQ(hexStringToInt64(""), std::nullopt);
    EXPECT_EQ(hexStringToInt64("0x"), std::nullopt);
    EXPECT_EQ(hexStringToInt64("xyz"), std::nullopt);
    EXPECT_EQ(hexStringToInt64("ffffffffffffffff"), std::nullopt);
}

TEST(HexStringToUint64, ParsesValidValues)
{
    EXPECT_EQ(hexStringToUint64("ff"), 255U);
    EXPECT_EQ(hexStringToUint64("0xff"), 255U);
    EXPECT_EQ(hexStringToUint64("FFFFFFFFFFFFFFFF"),
              std::numeric_limits<uint64_t>::max());
}

TEST(HexStringToUint64, RejectsInvalidValues)
{
    EXPECT_EQ(hexStringToUint64(""), std::nullopt);
    EXPECT_EQ(hexStringToUint64("0x"), std::nullopt);
    EXPECT_EQ(hexStringToUint64("12xyz"), std::nullopt);
}

} // namespace
