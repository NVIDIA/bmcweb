// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: Copyright OpenBMC Authors
#include "http/parsing.hpp"

#include <cstddef>
#include <format>
#include <string>

#include <gtest/gtest.h>

namespace
{

// Makes an array of n elements
std::string makeWideArray(int n)
{
    n -= 1; // The array itself counts as one element
    std::string os;
    os += "[";
    for (int i = 0; i < n; ++i)
    {
        if (i != 0)
        {
            os += ",";
        }
        os += std::to_string(i);
    }
    os += "]";
    return os;
}

std::string makeDeepArray(int n)
{
    std::string os;
    for (int i = 0; i < n; ++i)
    {
        os += "[";
    }
    for (int i = 0; i < n; ++i)
    {
        os += "]";
    }
    return os;
}
// Makes an object of n elements deep
std::string makeDeepObject(int n)
{
    std::string os;
    for (int i = 0; i < n; ++i)
    {
        os += std::format(R"({{"{}": )", i);
    }
    os += "null";
    for (int i = 0; i < n; ++i)
    {
        os += "}";
    }
    return os;
}

std::string makeWideObject(int n)
{
    std::string os;
    os += "{";
    for (int i = 0; i < n; ++i)
    {
        os += std::format(R"("{}": {})", i, i);
        if (i != n - 1)
        {
            os += ",";
        }
    }
    os += "}";
    return os;
}

TEST(HttpParsing, isJsonContentType)
{
    EXPECT_TRUE(isJsonContentType("application/json"));

    // The Redfish specification DSP0266 shows no space between the ; and
    // charset.
    EXPECT_TRUE(isJsonContentType("application/json;charset=utf-8"));
    EXPECT_TRUE(isJsonContentType("application/json;charset=ascii"));

    // Sites like mozilla show the space included [1]
    //  https://developer.mozilla.org/en-US/docs/Web/HTTP/Headers/Content-Type
    EXPECT_TRUE(isJsonContentType("application/json; charset=utf-8"));

    EXPECT_TRUE(isJsonContentType("APPLICATION/JSON"));
    EXPECT_TRUE(isJsonContentType("APPLICATION/JSON; CHARSET=UTF-8"));
    EXPECT_TRUE(isJsonContentType("APPLICATION/JSON;CHARSET=UTF-8"));

    EXPECT_FALSE(isJsonContentType("application/xml"));
    EXPECT_FALSE(isJsonContentType(""));
    EXPECT_FALSE(isJsonContentType(";"));
    EXPECT_FALSE(isJsonContentType("application/json;"));
    EXPECT_FALSE(isJsonContentType("application/json; "));
    EXPECT_FALSE(isJsonContentType("json"));
}

TEST(HttpParsing, parseRequestAsJsonLimitsArrayDepth)
{
    EXPECT_TRUE(parseStringAsJson(makeDeepArray(10)))
        << "10 level deep should parse";

    EXPECT_FALSE(parseStringAsJson(makeDeepArray(11)))
        << "11 level deep should fail to parse";
}

TEST(HttpParsing, parseRequestAsJsonLimitsObjectDepths)
{
    EXPECT_TRUE(parseStringAsJson(makeDeepObject(10)))
        << "10 level deep should parse";
    EXPECT_FALSE(parseStringAsJson(makeDeepObject(11)))
        << "11 level deep should fail to parse";
}

TEST(HttpParsing, parseStringAsJsonMaxValues)
{
    EXPECT_TRUE(parseStringAsJson(makeWideArray(200000)))
        << "200000 values should parse";
    EXPECT_FALSE(parseStringAsJson(makeWideArray(200001)))
        << "200001 values should be rejected";

    // Keys and values are each counted separately, and the root object also
    // counts as one value, so 99999 keys is the largest accepted object.
    EXPECT_TRUE(parseStringAsJson(makeWideObject(99999)))
        << "99999-key object should parse";
    EXPECT_FALSE(parseStringAsJson(makeWideObject(100000)))
        << "100000-key object should be rejected";
}

TEST(HttpParsing, parseStringAsJsonBodySizeLimit)
{
    constexpr size_t cap = 2097152U;

    // A well-formed JSON string of exactly the cap should parse.
    std::string atCap;
    atCap.reserve(cap);
    atCap += '"';
    atCap.append(cap - 2, 'a');
    atCap += '"';
    EXPECT_TRUE(parseStringAsJson(atCap)) << "Body at 2 MiB should parse";

    // One byte over the cap must be rejected before SAX parsing.
    std::string overCap;
    overCap.reserve(cap + 1);
    overCap += '"';
    overCap.append(cap - 1, 'a');
    overCap += '"';
    EXPECT_FALSE(parseStringAsJson(overCap))
        << "Body over 2 MiB should be rejected";
}

} // namespace
