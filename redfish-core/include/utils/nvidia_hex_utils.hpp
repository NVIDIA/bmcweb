// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: Copyright OpenBMC Authors
#pragma once

#include "hex_utils.hpp"
#include "logging.hpp"

#include <array>
#include <climits>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

static constexpr std::array<char, 16> nvidiaDigitsArray = {
    '0', '1', '2', '3', '4', '5', '6', '7',
    '8', '9', 'A', 'B', 'C', 'D', 'E', 'F'};

inline std::vector<std::string> intToHexByteArray(
    uint32_t value, size_t digits = sizeof(uint32_t) << 1)
{
    std::string rc(digits, '0');
    size_t bitIndex = (digits - 1) * 4;
    for (size_t digitIndex = 0; digitIndex < digits; digitIndex++)
    {
        rc[digitIndex] = nvidiaDigitsArray[(value >> bitIndex) & 0x0f];
        bitIndex -= 4;
    }

    size_t len = 2;
    std::vector<std::string> hexArray;
    for (auto i = digits; i >= len; i = i - len)
    {
        hexArray.push_back("0x" + rc.substr(i - len, 2));
    }

    return hexArray;
}

inline std::string vectorTo256BitHexString(const std::vector<uint8_t>& value)
{
    // Ensure the vector has exactly 32 bytes (256 bits)
    if (value.size() != 32)
    {
        BMCWEB_LOG_ERROR("vectorToHexString failed");
        return "";
    }

    // Convert the vector to a hex string
    std::stringstream ss;
    ss << "0x";
    for (const auto& byte : value)
    {
        char hi = nvidiaDigitsArray[(byte >> 4) & 0x0F];
        char lo = nvidiaDigitsArray[byte & 0x0F];
        ss << hi << lo;
    }
    // add logic to remove leading 0s
    std::string result = ss.str();
    // Remove leading zeros
    size_t firstNonZero = 2; // Start after "0x"
    while (firstNonZero < result.length() && result[firstNonZero] == '0')
    {
        ++firstNonZero;
    }

    // If all digits are zero, return ""
    if (firstNonZero == result.length())
    {
        return "0x0";
    }

    // Return the result with leading zeros removed
    return "0x" + result.substr(firstNonZero);
}

inline std::optional<std::vector<uint8_t>> stringNibbleToVector(
    std::string_view nibbleString)
{
    if (nibbleString.starts_with("0x"))
    {
        nibbleString.remove_prefix(2);
    }

    if (nibbleString.empty() || nibbleString.length() > 64)
    {
        return std::nullopt;
    }

    std::string processedString = std::string(64 - nibbleString.length(), '0') +
                                  std::string(nibbleString);

    std::vector<uint8_t> result = hexStringToBytes(processedString);
    if (result.empty())
    {
        return std::nullopt;
    }

    return result;
}
