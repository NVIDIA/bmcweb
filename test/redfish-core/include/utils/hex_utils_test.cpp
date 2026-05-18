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

TEST(IntToHexString, ReturnsCorrectHexForUint64)
{
    EXPECT_EQ(intToHexString(0xFFFFFFFFFFFFFFFFULL, 16), "FFFFFFFFFFFFFFFF");

    EXPECT_EQ(intToHexString(0, 4), "0000");
    EXPECT_EQ(intToHexString(0, 8), "00000000");
    EXPECT_EQ(intToHexString(0, 12), "000000000000");
    EXPECT_EQ(intToHexString(0, 16), "0000000000000000");

    // uint64_t sized ints
    EXPECT_EQ(intToHexString(0xDEADBEEFBAD4F00DULL, 4), "F00D");
    EXPECT_EQ(intToHexString(0xDEADBEEFBAD4F00DULL, 8), "BAD4F00D");
    EXPECT_EQ(intToHexString(0xDEADBEEFBAD4F00DULL, 12), "BEEFBAD4F00D");
    EXPECT_EQ(intToHexString(0xDEADBEEFBAD4F00DULL, 16), "DEADBEEFBAD4F00D");

    // uint16_t sized ints
    EXPECT_EQ(intToHexString(0xBEEF, 1), "F");
    EXPECT_EQ(intToHexString(0xBEEF, 2), "EF");
    EXPECT_EQ(intToHexString(0xBEEF, 3), "EEF");
    EXPECT_EQ(intToHexString(0xBEEF, 4), "BEEF");
}

TEST(BytesToHexString, OnSuccess)
{
    EXPECT_EQ(bytesToHexString({0x1a, 0x2b}), "1A2B");
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
    EXPECT_EQ(stringToInt64("ff", 16), 255);
    EXPECT_EQ(stringToInt64("7fffffffffffffff", 16),
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
    EXPECT_EQ(stringToUint64("ff", 16), 255U);
    EXPECT_EQ(stringToUint64("FFFFFFFFFFFFFFFF", 16),
              std::numeric_limits<uint64_t>::max());
}

TEST(StringToUint64, RejectsInvalidValues)
{
    EXPECT_EQ(stringToUint64(""), std::nullopt);
    EXPECT_EQ(stringToUint64("-1"), std::nullopt);
    EXPECT_EQ(stringToUint64("12xyz"), std::nullopt);
    EXPECT_EQ(stringToUint64("999999999999999999999999"), std::nullopt);
}

} // namespace
