// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: Copyright OpenBMC Authors
#include "json_html_serializer.hpp"

#include <nlohmann/json.hpp>

#include <cstddef>
#include <string>

#include <gtest/gtest.h>

namespace json_html_util
{
namespace
{

const std::string boilerplateStart =
    "<html>\n"
    "<head>\n"
    "<title>Redfish API</title>\n"
    "<link href=\"/styles/redfish.css\" rel=\"stylesheet\">\n"
    "</head>\n"
    "<body>\n"
    "<div class=\"container\">\n"
    "<img src=\"/images/DMTF_Redfish_logo_2017.svg\" alt=\"redfish\" height=\"406px\" width=\"576px\">\n";

const std::string boilerplateEnd =
    "</div>\n"
    "</body>\n"
    "</html>\n";

TEST(JsonHtmlSerializer, dumpHtmlLink)
{
    std::string out;
    nlohmann::json j;
    j["@odata.id"] = "/redfish/v1";
    dumpHtml(out, j);
    EXPECT_EQ(
        out,
        boilerplateStart +
            "<div class=\"content\">\n"
            "{<div class=tab>&quot@odata.id&quot: <a href=\"/redfish/v1\">\"/redfish/v1\"</a><br></div>}</div>\n" +
            boilerplateEnd);
}

TEST(JsonHtmlSerializer, dumpint)
{
    std::string out;
    nlohmann::json j = 42;
    dumpHtml(out, j);
    EXPECT_EQ(out, boilerplateStart + "<div class=\"content\">\n42</div>\n" +
                       boilerplateEnd);
}

TEST(JsonHtmlSerializer, dumpstring)
{
    std::string out;
    nlohmann::json j = "foobar";
    dumpHtml(out, j);
    EXPECT_EQ(out,
              boilerplateStart + "<div class=\"content\">\n\"foobar\"</div>\n" +
                  boilerplateEnd);
}

// Each invalid UTF-8 byte is escaped to the six-byte sequence "\ufffd".
// Long runs push the escape buffer past its 512-byte capacity; the reject
// branch must flush so the serializer neither overflows nor drops output.
// Lengths span just below, at and above the 512/6 ~= 85 byte boundary.
TEST(JsonHtmlSerializer, dumpInvalidUtf8RunDoesNotOverflow)
{
    for (size_t n : {1U, 84U, 85U, 86U, 100U, 300U})
    {
        std::string out;
        nlohmann::json j = std::string(n, '\xff');
        dumpHtml(out, j);

        std::string expected = "\"";
        for (size_t k = 0; k < n; ++k)
        {
            expected += "\\ufffd";
        }
        expected += "\"";
        std::string expectedHtml = boilerplateStart;
        expectedHtml += "<div class=\"content\">\n";
        expectedHtml += expected;
        expectedHtml += "</div>\n";
        expectedHtml += boilerplateEnd;
        EXPECT_EQ(out, expectedHtml);
    }
}

// Valid characters preceding an invalid run exercise the reject branch
// starting at every offset inside the buffer.
TEST(JsonHtmlSerializer, dumpMixedValidInvalidUtf8)
{
    for (size_t k : {0U, 83U, 84U, 85U, 500U})
    {
        std::string out;
        nlohmann::json j = std::string(k, 'a') + std::string(100, '\xff');
        dumpHtml(out, j);

        std::string expected = "\"";
        expected += std::string(k, 'a');
        for (size_t x = 0; x < 100; ++x)
        {
            expected += "\\ufffd";
        }
        expected += "\"";
        std::string expectedHtml = boilerplateStart;
        expectedHtml += "<div class=\"content\">\n";
        expectedHtml += expected;
        expectedHtml += "</div>\n";
        expectedHtml += boilerplateEnd;
        EXPECT_EQ(out, expectedHtml);
    }
}
} // namespace
} // namespace json_html_util
