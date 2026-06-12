// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: Copyright OpenBMC Authors
#include "http/http_client.hpp"

#include <boost/beast/http/field.hpp>
#include <boost/beast/http/status.hpp>

#include "gtest/gtest.h"

namespace crow
{
namespace
{

using HttpResponse = boost::beast::http::response<bmcweb::HttpBody>;

HttpResponse makeResponse(boost::beast::http::status status,
                          std::string_view contentType,
                          std::string_view contentLength)
{
    HttpResponse res;
    res.result(status);
    if (!contentType.empty())
    {
        res.set(boost::beast::http::field::content_type, contentType);
    }
    if (!contentLength.empty())
    {
        res.set(boost::beast::http::field::content_length, contentLength);
    }
    return res;
}

// parseStreamingContentLength tests

TEST(ParseStreamingContentLength, PresentAndValid)
{
    HttpResponse res =
        makeResponse(boost::beast::http::status::ok, "", "1234567");
    EXPECT_EQ(parseStreamingContentLength(res), 1234567U);
}

TEST(ParseStreamingContentLength, Missing)
{
    HttpResponse res;
    res.result(boost::beast::http::status::ok);
    EXPECT_EQ(parseStreamingContentLength(res), 0U);
}

TEST(ParseStreamingContentLength, NonNumericReturnsZero)
{
    HttpResponse res = makeResponse(boost::beast::http::status::ok, "", "abc");
    EXPECT_EQ(parseStreamingContentLength(res), 0U);
}

TEST(ParseStreamingContentLength, ZeroReturnsZero)
{
    HttpResponse res = makeResponse(boost::beast::http::status::ok, "", "0");
    EXPECT_EQ(parseStreamingContentLength(res), 0U);
}

// shouldStreamResponse tests

TEST(ShouldStreamResponse, BinaryResponseWithContentStreams)
{
    HttpResponse res = makeResponse(boost::beast::http::status::ok,
                                    "application/octet-stream", "1000000");
    EXPECT_TRUE(shouldStreamResponse(res, 1000000, false));
}

TEST(ShouldStreamResponse, JsonResponseIsBuffered)
{
    HttpResponse res = makeResponse(boost::beast::http::status::ok,
                                    "application/json", "1000");
    EXPECT_FALSE(shouldStreamResponse(res, 1000, false));
}

TEST(ShouldStreamResponse, JsonWithCharsetIsBuffered)
{
    HttpResponse res = makeResponse(boost::beast::http::status::ok,
                                    "application/json;charset=utf-8", "1000");
    EXPECT_FALSE(shouldStreamResponse(res, 1000, false));
}

TEST(ShouldStreamResponse, ZeroContentLengthIsBuffered)
{
    HttpResponse res = makeResponse(boost::beast::http::status::ok,
                                    "application/octet-stream", "0");
    EXPECT_FALSE(shouldStreamResponse(res, 0, false));
}

TEST(ShouldStreamResponse, NoContentStatusIsBuffered)
{
    HttpResponse res;
    res.result(boost::beast::http::status::no_content);
    EXPECT_FALSE(shouldStreamResponse(res, 0, false));
}

TEST(ShouldStreamResponse, InvalidResponseIsBuffered)
{
    HttpResponse res =
        makeResponse(boost::beast::http::status::internal_server_error,
                     "application/octet-stream", "1000");
    EXPECT_FALSE(shouldStreamResponse(res, 1000, /*responseIsInvalid=*/true));
}

TEST(ShouldStreamResponse, NoContentTypeWithBodyStreams)
{
    HttpResponse res =
        makeResponse(boost::beast::http::status::ok, "", "5000000");
    EXPECT_TRUE(shouldStreamResponse(res, 5000000, false));
}

} // namespace
} // namespace crow
