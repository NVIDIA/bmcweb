// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: Copyright OpenBMC Authors
#include "duplicatable_file_handle.hpp"
#include "http/complete_response_fields.hpp"
#include "http/http_body.hpp"
#include "http/http_response.hpp"
#include "utility.hpp"

#include <boost/beast/core/buffers_to_string.hpp>
#include <boost/beast/core/file_base.hpp>
#include <boost/beast/http/serializer.hpp>
#include <boost/beast/http/status.hpp>

#include <filesystem>
#include <string>
#include <utility>

#include "gtest/gtest.h"
namespace crow
{
namespace
{
void addHeaders(Response& res)
{
    res.addHeader("myheader", "myvalue");
    res.keepAlive(true);
    res.result(boost::beast::http::status::ok);
}
void verifyHeaders(Response& res)
{
    EXPECT_EQ(res.getHeaderValue("myheader"), "myvalue");
    EXPECT_EQ(res.keepAlive(), true);
    EXPECT_EQ(res.result(), boost::beast::http::status::ok);
}

std::string getData(boost::beast::http::response<bmcweb::HttpBody>& m)
{
    std::string ret;

    boost::beast::http::response_serializer<bmcweb::HttpBody> sr{m};
    sr.split(true);
    // Reads buffers into ret
    auto reader =
        [&sr, &ret](const boost::system::error_code& ec2, const auto& buffer) {
            EXPECT_FALSE(ec2);
            std::string ret2 = boost::beast::buffers_to_string(buffer);
            sr.consume(ret2.size());
            ret += ret2;
        };
    boost::system::error_code ec;

    // Read headers
    while (!sr.is_header_done())
    {
        sr.next(ec, reader);
        EXPECT_FALSE(ec);
    }
    ret.clear();

    // Read body
    while (!sr.is_done())
    {
        sr.next(ec, reader);
        EXPECT_FALSE(ec);
    }

    return ret;
}

TEST(HttpResponse, Headers)
{
    Response res;
    addHeaders(res);
    verifyHeaders(res);
}

TEST(HttpResponse, HeaderCountReturnsTotalHeaders)
{
    Response res;
    res.addHeader("Header-A", "value-a");
    res.addHeader("Header-B", "value-b");
    res.addHeader("Header-C", "value-c");

    EXPECT_EQ(res.headerCount(), 3U);
    EXPECT_EQ(res.getHeaderValue("Header-A"), "value-a");
    EXPECT_EQ(res.getHeaderValue("Header-B"), "value-b");
    EXPECT_EQ(res.getHeaderValue("Header-C"), "value-c");
}

TEST(HttpResponse, StringBody)
{
    Response res;
    addHeaders(res);
    std::string_view bodyValue = "this is my new body";
    res.write({bodyValue.data(), bodyValue.length()});
    EXPECT_EQ(*res.body(), bodyValue);
    verifyHeaders(res);
}
TEST(HttpResponse, HttpBody)
{
    Response res;
    addHeaders(res);
    DuplicatableFileHandle temporaryFile("sample text");
    res.openFile(temporaryFile.filePath);

    verifyHeaders(res);
}
TEST(HttpResponse, HttpBodyWithFd)
{
    Response res;
    addHeaders(res);
    DuplicatableFileHandle temporaryFile("sample text");
    boost::system::error_code ec;
    DuplicatableFileHandle fh;
    fh.fileHandle.open(temporaryFile.filePath.c_str(),
                       boost::beast::file_mode::read, ec);
    ASSERT_FALSE(ec);
    res.openFd(std::move(fh));
    verifyHeaders(res);
}

TEST(HttpResponse, Base64HttpBodyWithFd)
{
    Response res;
    addHeaders(res);
    DuplicatableFileHandle temporaryFile("sample text");
    boost::system::error_code ec;
    DuplicatableFileHandle fh;
    fh.fileHandle.open(temporaryFile.filePath.c_str(),
                       boost::beast::file_mode::read, ec);
    ASSERT_FALSE(ec);
    res.openFd(std::move(fh), bmcweb::EncodingType::Base64);
    verifyHeaders(res);
}

TEST(HttpResponse, BodyTransitions)
{
    Response res;
    addHeaders(res);
    DuplicatableFileHandle temporaryFile("sample text");
    res.openFile(temporaryFile.filePath);

    verifyHeaders(res);
    res.write("body text");

    verifyHeaders(res);
}

std::string generateBigdata()
{
    std::string result;
    while (result.size() < 10000)
    {
        result += "sample text";
    }
    return result;
}

TEST(HttpResponse, StringBodyWriterLarge)
{
    Response res;
    std::string data = generateBigdata();
    res.write(std::string(data));
    EXPECT_EQ(getData(res.response), data);
}

TEST(HttpResponse, Base64HttpBodyWriter)
{
    Response res;
    std::string data = "sample text";
    DuplicatableFileHandle temporaryFile(data);
    boost::system::error_code ec;
    DuplicatableFileHandle fh;
    fh.fileHandle.open(temporaryFile.filePath.c_str(),
                       boost::beast::file_mode::read, ec);
    ASSERT_FALSE(ec);
    res.openFd(std::move(fh), bmcweb::EncodingType::Base64);
    EXPECT_EQ(getData(res.response), "c2FtcGxlIHRleHQ=");
}

TEST(HttpResponse, Base64HttpBodyWriterLarge)
{
    Response res;
    std::string data = generateBigdata();
    DuplicatableFileHandle temporaryFile(data);
    boost::system::error_code ec;
    DuplicatableFileHandle fh;
    fh.fileHandle.open(temporaryFile.filePath.c_str(),
                       boost::beast::file_mode::read, ec);
    EXPECT_EQ(ec.value(), 0);
    res.openFd(std::move(fh), bmcweb::EncodingType::Base64);
    EXPECT_EQ(getData(res.response), utility::base64encode(data));
}

TEST(HttpResponse, HttpBodyWriterLarge)
{
    Response res;
    std::string data = generateBigdata();
    DuplicatableFileHandle temporaryFile(data);
    boost::system::error_code ec;
    DuplicatableFileHandle fh;
    fh.fileHandle.open(temporaryFile.filePath.c_str(),
                       boost::beast::file_mode::read, ec);
    EXPECT_EQ(ec.value(), 0);
    res.openFd(std::move(fh));
    EXPECT_EQ(getData(res.response), data);
}

TEST(HttpResponse, ZstdHandleEncodingDoesNotCloseFileFd)
{
    Response res;
    std::string data = "sample text";
    DuplicatableFileHandle temporaryFile(data);
    res.openFile(temporaryFile.filePath);

    ASSERT_TRUE(res.response.body().file().is_open());

    handleEncoding("zstd", res);

    EXPECT_TRUE(res.response.body().file().is_open());
    EXPECT_EQ(getData(res.response), data);
}

// Response with a non-empty body and 200 status, so that an ETag is computed
crow::Response makeEtagResponse()
{
    crow::Response res;
    res.result(boost::beast::http::status::ok);
    res.jsonValue["Password"] = nullptr;
    res.jsonValue["UserName"] = "user";
    return res;
}

TEST(HttpResponse, EtagWithoutSaltIsStable)
{
    crow::Response a = makeEtagResponse();
    crow::Response b = makeEtagResponse();
    EXPECT_FALSE(a.getCurrentEtag().empty());
    EXPECT_EQ(a.getCurrentEtag(), b.getCurrentEtag());
}

TEST(HttpResponse, EtagSaltChangesEtag)
{
    crow::Response res = makeEtagResponse();
    std::string unsalted = res.getCurrentEtag();

    res.setEtagSalt("1:1");
    std::string salted = res.getCurrentEtag();
    EXPECT_NE(salted, unsalted);
    // Same salt gives the same ETag
    EXPECT_EQ(res.getCurrentEtag(), salted);

    res.setEtagSalt("1:2");
    EXPECT_NE(res.getCurrentEtag(), salted);
    EXPECT_NE(res.getCurrentEtag(), unsalted);

    // An empty salt gives back the unsalted ETag
    res.setEtagSalt("");
    EXPECT_EQ(res.getCurrentEtag(), unsalted);
}

TEST(HttpResponse, EtagSaltIsClearedByClear)
{
    crow::Response res = makeEtagResponse();
    std::string unsalted = res.getCurrentEtag();
    res.setEtagSalt("1:1");
    ASSERT_NE(res.getCurrentEtag(), unsalted);

    res.clear();
    res.result(boost::beast::http::status::ok);
    res.jsonValue["Password"] = nullptr;
    res.jsonValue["UserName"] = "user";
    EXPECT_EQ(res.getCurrentEtag(), unsalted);
}

TEST(HttpResponse, EtagSaltSurvivesMove)
{
    crow::Response res = makeEtagResponse();
    res.setEtagSalt("1:1");
    std::string salted = res.getCurrentEtag();

    crow::Response moved(std::move(res));
    EXPECT_EQ(moved.getCurrentEtag(), salted);

    crow::Response assigned;
    assigned = std::move(moved);
    EXPECT_EQ(assigned.getCurrentEtag(), salted);
}

TEST(HttpResponse, EtagHeaderUsesSalt)
{
    crow::Response plain = makeEtagResponse();
    plain.setResponseEtagAndHandleNotModified();
    std::string plainHeader(plain.getHeaderValue("ETag"));

    crow::Response salted = makeEtagResponse();
    salted.setEtagSalt("1:1");
    salted.setResponseEtagAndHandleNotModified();
    std::string saltedHeader(salted.getHeaderValue("ETag"));

    EXPECT_FALSE(plainHeader.empty());
    EXPECT_FALSE(saltedHeader.empty());
    EXPECT_NE(plainHeader, saltedHeader);
}

TEST(HttpResponse, IfNoneMatchComparesSaltedEtag)
{
    crow::Response first = makeEtagResponse();
    first.setEtagSalt("1:1");
    std::string current = first.getCurrentEtag();

    // Client sends the ETag it got for the current state: not modified
    crow::Response same = makeEtagResponse();
    same.setExpectedEtag(current);
    same.setEtagSalt("1:1");
    same.setResponseEtagAndHandleNotModified();
    EXPECT_EQ(same.result(), boost::beast::http::status::not_modified);

    // The salt changed since (e.g. password changed): full response
    crow::Response changed = makeEtagResponse();
    changed.setExpectedEtag(current);
    changed.setEtagSalt("1:2");
    changed.setResponseEtagAndHandleNotModified();
    EXPECT_EQ(changed.result(), boost::beast::http::status::ok);
}

} // namespace
} // namespace crow
