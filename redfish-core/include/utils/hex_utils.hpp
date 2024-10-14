#pragma once

#include <array>
#include <climits>
#include <cstddef>
#include <cstdint>
#include <iomanip>
#include <sstream>
#include <string>
#include <vector>

static constexpr std::array<char, 16> digitsArray = {
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
        rc[i * 2 + 1] = digitsArray[bytes[i] & 0x0f];
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

inline std::vector<std::string>
    intToHexByteArray(uint32_t value, size_t digits = sizeof(uint32_t) << 1)
{
    std::string rc(digits, '0');
    size_t bitIndex = (digits - 1) * 4;
    for (size_t digitIndex = 0; digitIndex < digits; digitIndex++)
    {
        rc[digitIndex] = digitsArray[(value >> bitIndex) & 0x0f];
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
        return "0x" + std::string(64, '0');
    }

    // Convert the vector to a hex string
    std::stringstream ss;
    ss << "0x";
    for (const auto& byte : value)
    {
        ss << std::hex << std::setw(2) << std::setfill('0')
           << static_cast<int>(byte);
    }
    return ss.str();
}
