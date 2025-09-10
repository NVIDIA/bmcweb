// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: Copyright OpenBMC Authors
#pragma once

#include "logging.hpp"
#include <array>
#include <climits>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <regex>
#include <sstream>
#include <string>
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

inline std::vector<uint8_t> stringNibbleToVector(
    const std::string& nibbleString)
{
    std::vector<uint8_t> result(32, 0); // Initialize with 32 zeros

    // Validate input string
    std::string processedString = nibbleString;

    // Remove '0x' prefix if present
    if (processedString.size() >= 2 && processedString[0] == '0' &&
        processedString[1] == 'x')
    {
        processedString.erase(0, 2);
    }

    // Check for even length
    if (processedString.length() > 64)
    {
        throw std::invalid_argument("Input string is too long");
    }

    // Validate hexadecimal characters
    std::regex hexRegex("^[0-9A-Fa-f]+$");
    if (!std::regex_match(processedString, hexRegex))
    {
        throw std::invalid_argument(
            "Input string contains invalid hexadecimal characters");
    }

    // Pad the string with leading zeros if necessary
    processedString =
        std::string(64 - processedString.length(), '0') + processedString;

    for (size_t i = 0; i < 32; ++i)
    {
        std::string byteString = processedString.substr(i * 2, 2);
        result[i] = static_cast<uint8_t>(std::stoi(byteString, nullptr, 16));
    }

    return result;
}