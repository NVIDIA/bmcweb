#include "http_utility.hpp"

#include <array>

#include <gtest/gtest.h>

namespace http_helpers
{
namespace
{

TEST(isContentTypeAllowed, PositiveTest)
{
    EXPECT_TRUE(isContentTypeAllowed("*/*", ContentType::HTML, true));
    EXPECT_TRUE(isContentTypeAllowed("application/octet-stream",
                                     ContentType::OctetStream, false));
    EXPECT_TRUE(isContentTypeAllowed("text/html", ContentType::HTML, false));
    EXPECT_TRUE(
        isContentTypeAllowed("application/json", ContentType::JSON, false));
    EXPECT_TRUE(
        isContentTypeAllowed("application/cbor", ContentType::CBOR, false));
    EXPECT_TRUE(isContentTypeAllowed("application/json, text/html",
                                     ContentType::HTML, false));
}

TEST(isContentTypeAllowed, NegativeTest)
{
    EXPECT_FALSE(isContentTypeAllowed("application/octet-stream",
                                      ContentType::HTML, false));
    EXPECT_FALSE(
        isContentTypeAllowed("application/html", ContentType::JSON, false));
    EXPECT_FALSE(
        isContentTypeAllowed("application/json", ContentType::CBOR, false));
    EXPECT_FALSE(
        isContentTypeAllowed("application/cbor", ContentType::HTML, false));
    EXPECT_FALSE(isContentTypeAllowed("application/json, text/html",
                                      ContentType::OctetStream, false));
}

TEST(isContentTypeAllowed, ContainsAnyMimeTypeReturnsTrue)
{
    EXPECT_TRUE(
        isContentTypeAllowed("text/html, */*", ContentType::OctetStream, true));
}

TEST(isContentTypeAllowed, ContainsQFactorWeightingReturnsTrue)
{
    EXPECT_TRUE(isContentTypeAllowed("text/html, */*;q=0.8",
                                     ContentType::OctetStream, true));
}

TEST(getPreferredContentType, PositiveTest)
{
    std::array<ContentType, 1> contentType{ContentType::HTML};
    EXPECT_EQ(
        getPreferredContentType("text/html, application/json", contentType),
        ContentType::HTML);

    std::array<ContentType, 2> htmlJson{ContentType::HTML, ContentType::JSON};
    EXPECT_EQ(getPreferredContentType("text/html, application/json", htmlJson),
              ContentType::HTML);

    std::array<ContentType, 2> jsonHtml{ContentType::JSON, ContentType::HTML};
    EXPECT_EQ(getPreferredContentType("text/html, application/json", jsonHtml),
              ContentType::HTML);

    std::array<ContentType, 2> cborJson{ContentType::CBOR, ContentType::JSON};
    EXPECT_EQ(getPreferredContentType("application/cbor, application::json",
                                      cborJson),
              ContentType::CBOR);

    EXPECT_EQ(getPreferredContentType("application/json", cborJson),
              ContentType::JSON);
    EXPECT_EQ(getPreferredContentType("*/*", cborJson), ContentType::ANY);
}

TEST(getPreferredContentType, NegativeTest)
{
    std::array<ContentType, 1> contentType{ContentType::CBOR};
    EXPECT_EQ(
        getPreferredContentType("text/html, application/json", contentType),
        ContentType::NoMatch);
}

TEST(headerContains, PositiveTest)
{
    EXPECT_TRUE(headerContains("chunked", "chunked"));
    EXPECT_TRUE(headerContains("chunked;q=0.8", "chunked"));
    EXPECT_TRUE(
        headerContains("chunked, br;q=1.0, gzip;q=0.8, *;q=0.1", "chunked"));
    EXPECT_TRUE(
        headerContains("br;q=1.0, chunked, gzip;q=0.8, *;q=0.1", "chunked"));
    EXPECT_TRUE(
        headerContains("br;q=1.0, gzip;q=0.8, chunked, *;q=0.1", "chunked"));
    EXPECT_TRUE(
        headerContains("br;q=1.0, gzip;q=0.8, *;q=0.1, chunked", "chunked"));
}

TEST(headerContains, NegativeTest)
{
    EXPECT_FALSE(headerContains("", "chunked"));
    EXPECT_FALSE(headerContains(std::string_view{}, "chunked"));
    EXPECT_FALSE(headerContains("nochunked;q=0.8", "chunked"));
    EXPECT_FALSE(headerContains("br;q=1.0, gzip;q=0.8, *;q=0.1", "chunked"));
}

} // namespace
} // namespace http_helpers
