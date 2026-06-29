// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: Copyright OpenBMC Authors
#pragma once

#include "logging.hpp"

#include <array>
#include <charconv>
#include <climits>
#include <cstddef>
#include <cstdint>
#include <iomanip>
#include <optional>
#include <regex>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

constexpr std::array<char, 16> digitsArray = {
    '0', '1', '2', '3', '4', '5', '6', '7',
    '8', '9', 'A', 'B', 'C', 'D', 'E', 'F'};

inline std::string intToHexString(uint64_t value, size_t digits)
{
    std::string rc(digits, '0');
    size_t bitIndex = (digits - 1) * 4;
    for (size_t digitIndex = 0; digitIndex < digits; digitIndex++)
    {
        rc[digitIndex] = digitsArray[(value >> bitIndex) & 0x0f];
        bitIndex -= 4;
    }
    return rc;
}

inline std::string bytesToHexString(const std::vector<uint8_t>& bytes)
{
    std::string rc(bytes.size() * 2, '0');
    for (size_t i = 0; i < bytes.size(); ++i)
    {
        rc[i * 2] = digitsArray[(bytes[i] & 0xf0) >> 4];
        rc[(i * 2) + 1] = digitsArray[bytes[i] & 0x0f];
    }
    return rc;
}

// Returns nibble.
inline uint8_t hexCharToNibble(char ch)
{
    uint8_t rc = 16;
    if (ch >= '0' && ch <= '9')
    {
        rc = static_cast<uint8_t>(ch) - '0';
    }
    else if (ch >= 'A' && ch <= 'F')
    {
        rc = static_cast<uint8_t>(ch - 'A') + 10U;
    }
    else if (ch >= 'a' && ch <= 'f')
    {
        rc = static_cast<uint8_t>(ch - 'a') + 10U;
    }

    return rc;
}

// Returns empty vector in case of malformed hex-string.
inline std::vector<uint8_t> hexStringToBytes(const std::string& str)
{
    std::vector<uint8_t> rc(str.size() / 2, 0);
    for (size_t i = 0; i < str.length(); i += 2)
    {
        uint8_t hi = hexCharToNibble(str[i]);
        if (i == str.length() - 1)
        {
            return {};
        }
        uint8_t lo = hexCharToNibble(str[i + 1]);
        if (lo == 16 || hi == 16)
        {
            return {};
        }

        rc[i / 2] = static_cast<uint8_t>(hi << 4) | lo;
    }
    return rc;
}

// Parse a base-10 signed integer from sv without throwing.  Returns
// std::nullopt if the input is empty, has trailing non-numeric characters, or
// overflows int64_t.  Use this instead of std::stoi/stol on any value sourced
// from HTTP input or D-Bus properties.
inline std::optional<int64_t> stringToInt64(std::string_view sv)
{
    int64_t value{};
    auto [ptr, ec] = std::from_chars(sv.begin(), sv.end(), value);
    if (ec != std::errc{} || ptr != sv.end())
    {
        return std::nullopt;
    }
    return value;
}

// Parse a base-10 unsigned integer from sv without throwing.  Returns
// std::nullopt if the input is empty, has trailing non-numeric characters, or
// overflows uint64_t.  Use this instead of std::stoul/stoull on any value
// sourced from HTTP input or D-Bus properties.
inline std::optional<uint64_t> stringToUint64(std::string_view sv)
{
    uint64_t value{};
    auto [ptr, ec] = std::from_chars(sv.begin(), sv.end(), value);
    if (ec != std::errc{} || ptr != sv.end())
    {
        return std::nullopt;
    }
    return value;
}

// Parse a base-16 signed integer from sv without throwing.  Accepts an
// optional "0x"/"0X" prefix.  Returns std::nullopt if the input is empty, has
// trailing non-hex characters, or overflows int64_t.  Use this instead of
// std::stoi/stol(.., 16) on any value sourced from HTTP input or D-Bus.
inline std::optional<int64_t> hexStringToInt64(std::string_view sv)
{
    if (sv.starts_with("0x") || sv.starts_with("0X"))
    {
        sv.remove_prefix(2);
    }
    int64_t value{};
    auto [ptr, ec] = std::from_chars(sv.begin(), sv.end(), value, 16);
    if (ec != std::errc{} || ptr != sv.end())
    {
        return std::nullopt;
    }
    return value;
}

// Parse a base-16 unsigned integer from sv without throwing.  Accepts an
// optional "0x"/"0X" prefix.  Returns std::nullopt if the input is empty, has
// trailing non-hex characters, or overflows uint64_t.  Use this instead of
// std::stoul/stoull(.., 16) on any value sourced from HTTP input or D-Bus.
inline std::optional<uint64_t> hexStringToUint64(std::string_view sv)
{
    if (sv.starts_with("0x") || sv.starts_with("0X"))
    {
        sv.remove_prefix(2);
    }
    uint64_t value{};
    auto [ptr, ec] = std::from_chars(sv.begin(), sv.end(), value, 16);
    if (ec != std::errc{} || ptr != sv.end())
    {
        return std::nullopt;
    }
    return value;
}
