// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: Copyright OpenBMC Authors
#pragma once

#include <algorithm>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace redfish
{

namespace stl_utils
{

template <typename ForwardIterator>
ForwardIterator firstDuplicate(ForwardIterator first, ForwardIterator last)
{
    auto newLast = first;

    for (auto current = first; current != last; ++current)
    {
        if (std::find(first, newLast, *current) == newLast)
        {
            if (newLast != current)
            {
                *newLast = *current;
            }
            ++newLast;
        }
    }

    return newLast;
}

template <typename T>
void removeDuplicate(T& t)
{
    t.erase(firstDuplicate(t.begin(), t.end()), t.end());
}

inline std::vector<uint8_t> getBytes(const std::string& s)
{
    std::vector<uint8_t> bytes;
    bytes.reserve(s.size());

    std::transform(std::begin(s), std::end(s), std::back_inserter(bytes),
                   [](char c) { return static_cast<uint8_t>(c); });

    return bytes;
}

inline uint8_t hexCharToInt(char ch)
{
    if (ch >= '0' && ch <= '9')
    {
        return static_cast<uint8_t>(ch - '0');
    }
    if (ch >= 'A' && ch <= 'F')
    {
        return static_cast<uint8_t>(ch - 'A' + 10);
    }
    if (ch >= 'a' && ch <= 'f')
    {
        return static_cast<uint8_t>(ch - 'a' + 10);
    }
    throw std::invalid_argument("Invalid character for hex");
}

inline std::vector<uint8_t> hexStringToVector(std::string_view str)
{
    if (str.size() % 2)
    {
        throw std::invalid_argument("String not an even number of characters");
    }
    std::vector<uint8_t> ret;
    ret.resize(str.size() / 2);
    for (size_t i = 0; i < ret.size(); ++i)
    {
        auto bits = hexCharToInt(str[i * 2]) << 4;
        bits |= hexCharToInt(str[i * 2 + 1]);
        ret[i] |= static_cast<uint8_t>(bits);
    }

    return ret;
}

} // namespace stl_utils
} // namespace redfish
