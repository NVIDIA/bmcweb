// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: Copyright OpenBMC Authors
#include "multipart_serializer.hpp"

#include <boost/beast/http/field.hpp>
#include <nlohmann/json.hpp>

#include <string>
#include <string_view>
#include <utility>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

namespace
{

using testing::MatchesRegex;

class MultipartSerializerTest : public ::testing::Test
{
  protected:
    std::string output;

    MultipartSerializer make()
    {
        return MultipartSerializer([this](std::string_view data) {
            output.append(data);
        });
    }
};

TEST_F(MultipartSerializerTest, BoundaryIsGeneratedAndAlphanumeric)
{
    MultipartSerializer s = make();
    std::string_view boundary = s.getBoundary();
    EXPECT_THAT(boundary,
                MatchesRegex("^------------------------[a-zA-Z0-9]{22}$"));
}

TEST_F(MultipartSerializerTest, EachInstanceGeneratesAUniqueBoundary)
{
    MultipartSerializer a = make();
    MultipartSerializer b = make();
    EXPECT_NE(a.getBoundary(), b.getBoundary());
}

TEST_F(MultipartSerializerTest, EmptyBodyOnlyHasClosingBoundary)
{
    MultipartSerializer s = make();
    std::string b(s.getBoundary());
    s.start();
    s.finish();
    EXPECT_EQ(output, "\r\n--" + b + "--\r\n");
}

TEST_F(MultipartSerializerTest, SinglePartWithFieldsAndBody)
{
    MultipartSerializer s = make();
    std::string b(s.getBoundary());
    boost::beast::http::fields fields;
    fields.set(boost::beast::http::field::content_type, "application/json");
    fields.set("Content-Id", "1");

    s.start();
    s.beginPart(fields);
    s.put(R"({"foo":"bar"})");
    s.finish();

    EXPECT_EQ(output, "--" + b +
                          "\r\n"
                          "Content-Type: application/json\r\n"
                          "Content-Id: 1\r\n"
                          "\r\n"
                          "{\"foo\":\"bar\"}"
                          "\r\n--" +
                          b + "--\r\n");
}

TEST_F(MultipartSerializerTest, MultiplePartsAreSeparatedByBoundary)
{
    MultipartSerializer s = make();
    std::string b(s.getBoundary());
    boost::beast::http::fields f1;
    f1.set(boost::beast::http::field::content_type, "text/plain");
    boost::beast::http::fields f2;
    f2.set(boost::beast::http::field::content_type, "application/octet-stream");

    s.start();
    s.beginPart(f1);
    s.put("hello");
    s.beginPart(f2);
    s.put("world");
    s.finish();

    EXPECT_EQ(output,
              "--" + b +
                  "\r\n"
                  "Content-Type: text/plain\r\n"
                  "\r\n"
                  "hello"
                  "\r\n--" +
                  b +
                  "\r\n"
                  "Content-Type: application/octet-stream\r\n"
                  "\r\n"
                  "world"
                  "\r\n--" +
                  b + "--\r\n");
}

TEST_F(MultipartSerializerTest, PartWithNoFieldsHasOnlyBlankHeaderSeparator)
{
    MultipartSerializer s = make();
    std::string b(s.getBoundary());
    boost::beast::http::fields fields;

    s.start();
    s.beginPart(fields);
    s.put("body");
    s.finish();

    EXPECT_EQ(output, "--" + b +
                          "\r\n"
                          "\r\n"
                          "body"
                          "\r\n--" +
                          b + "--\r\n");
}

TEST_F(MultipartSerializerTest, PutCanBeCalledMultipleTimesAndConcatenates)
{
    MultipartSerializer s = make();
    std::string b(s.getBoundary());
    boost::beast::http::fields fields;

    s.start();
    s.beginPart(fields);
    s.put("hello ");
    s.put("");
    s.put("world");
    s.finish();

    EXPECT_EQ(output, "--" + b +
                          "\r\n"
                          "\r\n"
                          "hello world"
                          "\r\n--" +
                          b + "--\r\n");
}

TEST_F(MultipartSerializerTest, PutWritesPayloadVerbatimWithoutEscaping)
{
    // The serializer should not perform any boundary detection / escaping on
    // the body.  Even payloads that look like a boundary should pass through
    // unchanged.
    MultipartSerializer s = make();
    std::string b(s.getBoundary());
    boost::beast::http::fields fields;

    std::string payload =
        "\r\n--" + b + "\r\nContent-Type: text/plain\r\n\r\nfake";

    s.start();
    s.beginPart(fields);
    s.put(payload);
    s.finish();

    std::string expected;
    expected += "--" + b + "\r\n";
    expected += "\r\n";
    expected += payload;
    expected += "\r\n--" + b + "--\r\n";
    EXPECT_EQ(output, expected);
}

TEST_F(MultipartSerializerTest, BinaryDataIsWrittenVerbatim)
{
    MultipartSerializer s = make();
    std::string b(s.getBoundary());
    boost::beast::http::fields fields;
    constexpr std::string_view binary("\x00\x01\x02\x03\xff\xfe", 6);

    s.start();
    s.beginPart(fields);
    s.put(binary);
    s.finish();

    std::string expected;
    expected += "--" + b + "\r\n";
    expected += "\r\n";
    expected.append(binary.data(), binary.size());
    expected += "\r\n--" + b + "--\r\n";
    EXPECT_EQ(output, expected);
}

TEST_F(MultipartSerializerTest, RepeatedFieldNamesPreserveInsertionOrder)
{
    MultipartSerializer s = make();
    std::string b(s.getBoundary());
    boost::beast::http::fields fields;
    fields.insert("X-Custom", "first");
    fields.insert("X-Custom", "second");

    s.start();
    s.beginPart(fields);
    s.finish();

    EXPECT_EQ(output, "--" + b +
                          "\r\n"
                          "X-Custom: first\r\n"
                          "X-Custom: second\r\n"
                          "\r\n"
                          "\r\n--" +
                          b + "--\r\n");
}

TEST_F(MultipartSerializerTest, PutJsonObjectSerializesAsCompactJson)
{
    MultipartSerializer s = make();
    std::string b(s.getBoundary());
    boost::beast::http::fields fields;
    fields.set(boost::beast::http::field::content_type, "application/json");

    nlohmann::json::object_t obj;
    obj["foo"] = "bar";
    obj["count"] = 3;

    s.start();
    s.beginPart(fields);
    s.putJsonObject(std::move(obj));
    s.finish();

    EXPECT_EQ(output, "--" + b +
                          "\r\n"
                          "Content-Type: application/json\r\n"
                          "\r\n"
                          "{\"count\":3,\"foo\":\"bar\"}"
                          "\r\n--" +
                          b + "--\r\n");
}

TEST_F(MultipartSerializerTest, PutJsonObjectEmptyObject)
{
    MultipartSerializer s = make();
    std::string b(s.getBoundary());
    boost::beast::http::fields fields;

    s.start();
    s.beginPart(fields);
    s.putJsonObject(nlohmann::json::object_t{});
    s.finish();

    EXPECT_EQ(output, "--" + b +
                          "\r\n"
                          "\r\n"
                          "{}"
                          "\r\n--" +
                          b + "--\r\n");
}

} // namespace
