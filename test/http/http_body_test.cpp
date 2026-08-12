// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: Copyright OpenBMC Authors
#include "duplicatable_file_handle.hpp"
#include "http_body.hpp"

#include <fcntl.h>
#include <sys/types.h>
#include <unistd.h>

#include <boost/beast/core/error.hpp>
#include <boost/beast/core/file_base.hpp>
#include <boost/beast/http/message.hpp>
#include <boost/system/errc.hpp>
#include <boost/system/error_code.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

using ::testing::ElementsAre;

namespace bmcweb
{
namespace
{

TEST(HttpHttpBodyValueType, MoveString)
{
    HttpBody::value_type value("teststring");
    HttpBody::value_type value2(std::move(value));
    EXPECT_EQ(value2.encodingType, EncodingType::Raw);
    EXPECT_EQ(value2.str(), "teststring");
    EXPECT_EQ(value2.payloadSize(), 10);
}

TEST(HttpHttpBodyValueType, MoveOperatorString)
{
    HttpBody::value_type value;
    value.str() = "teststring";
    HttpBody::value_type value2 = std::move(value);
    EXPECT_EQ(value2.encodingType, EncodingType::Raw);
    EXPECT_EQ(value2.str(), "teststring");
    EXPECT_EQ(value2.payloadSize(), 10);
}

TEST(HttpHttpBodyValueType, copysignl)
{
    HttpBody::value_type value;
    value.str() = "teststring";
    HttpBody::value_type value2(value);
    EXPECT_EQ(value2.encodingType, EncodingType::Raw);
    EXPECT_EQ(value2.str(), "teststring");
    EXPECT_EQ(value2.payloadSize(), 10);
}

TEST(HttpHttpBodyValueType, CopyOperatorString)
{
    HttpBody::value_type value;
    value.str() = "teststring";
    // Move constructor
    HttpBody::value_type value2 = value;
    EXPECT_EQ(value2.encodingType, EncodingType::Raw);
    EXPECT_EQ(value2.compressionType, CompressionType::Raw);
    EXPECT_EQ(value2.str(), "teststring");
    EXPECT_EQ(value2.payloadSize(), 10);
}

TEST(HttpHttpBodyValueType, MoveFile)
{
    HttpBody::value_type value(EncodingType::Base64, CompressionType::Raw);
    DuplicatableFileHandle temporaryFile("teststring");
    boost::system::error_code ec;
    value.open(temporaryFile.filePath.c_str(), boost::beast::file_mode::read,
               ec);
    ASSERT_FALSE(ec);
    HttpBody::value_type value2(std::move(value));
    std::array<char, 11> buffer{};
    size_t out = value2.file().read(buffer.data(), buffer.size(), ec);
    ASSERT_FALSE(ec);
    EXPECT_EQ(value2.encodingType, EncodingType::Base64);
    EXPECT_EQ(value2.compressionType, CompressionType::Raw);

    EXPECT_THAT(std::span(buffer.data(), out),
                ElementsAre('t', 'e', 's', 't', 's', 't', 'r', 'i', 'n', 'g'));

    EXPECT_THAT(buffer, ElementsAre('t', 'e', 's', 't', 's', 't', 'r', 'i', 'n',
                                    'g', '\0'));

    EXPECT_EQ(value2.payloadSize(), 16);
}

TEST(HttpHttpBodyValueType, MoveOperatorFile)
{
    HttpBody::value_type value(EncodingType::Base64, CompressionType::Raw);
    DuplicatableFileHandle temporaryFile("teststring");
    boost::system::error_code ec;
    value.open(temporaryFile.filePath.c_str(), boost::beast::file_mode::read,
               ec);
    ASSERT_FALSE(ec);
    HttpBody::value_type value2 = std::move(value);
    std::array<char, 11> buffer{};
    size_t out = value2.file().read(buffer.data(), buffer.size(), ec);
    ASSERT_FALSE(ec);
    EXPECT_EQ(value2.encodingType, EncodingType::Base64);
    EXPECT_EQ(value2.compressionType, CompressionType::Raw);

    EXPECT_THAT(std::span(buffer.data(), out),
                ElementsAre('t', 'e', 's', 't', 's', 't', 'r', 'i', 'n', 'g'));
    EXPECT_THAT(buffer, ElementsAre('t', 'e', 's', 't', 's', 't', 'r', 'i', 'n',
                                    'g', '\0'));

    EXPECT_EQ(value2.payloadSize(), 16);
}

TEST(HttpFileBodyValueType, SetFd)
{
    HttpBody::value_type value(EncodingType::Base64, CompressionType::Raw);
    DuplicatableFileHandle temporaryFile("teststring");
    boost::system::error_code ec;

    DuplicatableFileHandle fh;
    fh.fileHandle.open(temporaryFile.filePath.c_str(),
                       boost::beast::file_mode::read, ec);
    ASSERT_FALSE(ec);
    value.setFd(std::move(fh), ec);
    ASSERT_FALSE(ec);
    std::array<char, 4096> buffer{};

    size_t out = value.file().read(buffer.data(), buffer.size(), ec);
    ASSERT_FALSE(ec);

    EXPECT_THAT(std::span(buffer.data(), out),
                ElementsAre('t', 'e', 's', 't', 's', 't', 'r', 'i', 'n', 'g'));
    EXPECT_EQ(value.payloadSize(), 16);
}

TEST(HttpFileBodyValueType, SetStreamingReceiver)
{
    HttpBody::value_type value;
    EXPECT_FALSE(value.streamingReceiver);
    value.setStreamingReceiver(true);
    EXPECT_TRUE(value.streamingReceiver);
    value.setStreamingReceiver(false);
    EXPECT_FALSE(value.streamingReceiver);
}

TEST(HttpFileBodyValueType, SetFileSizeOverridesFstat)
{
    HttpBody::value_type value;
    DuplicatableFileHandle temporaryFile("teststring");
    boost::system::error_code ec;

    DuplicatableFileHandle fh;
    fh.fileHandle.open(temporaryFile.filePath.c_str(),
                       boost::beast::file_mode::read, ec);
    ASSERT_FALSE(ec);
    value.setFd(std::move(fh), ec);
    ASSERT_FALSE(ec);

    value.setFileSize(99);
    std::optional<std::uint64_t> size = value.payloadSize();
    ASSERT_TRUE(size.has_value());
    // NOLINTNEXTLINE(bugprone-unchecked-optional-access)
    EXPECT_EQ(*size, 99U);
}

TEST(HttpFileBodyValueType, ClearResetsToString)
{
    HttpBody::value_type value;
    DuplicatableFileHandle temporaryFile("teststring");
    boost::system::error_code ec;

    DuplicatableFileHandle fh;
    fh.fileHandle.open(temporaryFile.filePath.c_str(),
                       boost::beast::file_mode::read, ec);
    ASSERT_FALSE(ec);
    value.setFd(std::move(fh), ec);
    ASSERT_FALSE(ec);

    EXPECT_TRUE(value.file().is_open());

    value.clear();

    EXPECT_FALSE(value.file().is_open());
    EXPECT_EQ(value.str(), "");
    EXPECT_FALSE(value.streamingReceiver);
}

// BUG-2: EAGAIN path — path-A propagates ec; path-B delivers data after
// dead-code removal.

TEST(HttpBodyWriter, EagainOnEmptyNonBlockingPipe_ReturnsNone)
{
    std::array<int, 2> pipeFds{};
    ASSERT_EQ(pipe2(pipeFds.data(), O_NONBLOCK), 0);
    const int writeFd =
        pipeFds[1]; // keep write end open to avoid premature EOF

    HttpBody::value_type value;
    boost::system::error_code ec;
    value.setFd(DuplicatableFileHandle(pipeFds[0]), ec);
    ASSERT_FALSE(ec);

    boost::beast::http::header<false> hdr;
    HttpBody::writer w(hdr, value);
    boost::beast::error_code writerEc;
    HttpBody::writer::init(writerEc);

    auto result = w.get(writerEc);

    EXPECT_FALSE(result.has_value());
    EXPECT_TRUE(
        writerEc == boost::system::errc::resource_unavailable_try_again ||
        writerEc == boost::system::errc::operation_would_block);

    close(writeFd);
}

TEST(HttpBodyWriter, NonBlockingPipeWithData_ReturnsBuffer)
{
    std::array<int, 2> pipeFds{};
    ASSERT_EQ(pipe2(pipeFds.data(), O_NONBLOCK), 0);

    const std::string_view payload = "stream_payload";
    ASSERT_EQ(::write(pipeFds[1], payload.data(), payload.size()),
              static_cast<ssize_t>(payload.size()));
    close(pipeFds[1]); // signal EOF after the payload

    HttpBody::value_type value;
    boost::system::error_code ec;
    value.setFd(DuplicatableFileHandle(pipeFds[0]), ec);
    ASSERT_FALSE(ec);

    boost::beast::http::header<false> hdr;
    HttpBody::writer w(hdr, value);
    boost::beast::error_code writerEc;

    auto result = w.get(writerEc);

    ASSERT_FALSE(writerEc);
    ASSERT_TRUE(result.has_value());
    auto [buf, more] = *result;
    EXPECT_EQ(std::string_view(static_cast<const char*>(buf.data()),
                               buf.size()),
              payload);
}

// BUG-3: setFd always clears ec; regression anchor for when setFd becomes
// fallible.

TEST(HttpFileBodyValueType, SetFd_AlwaysClearsPassedInErrorCode)
{
    DuplicatableFileHandle temporaryFile("test content");
    DuplicatableFileHandle fh;
    boost::system::error_code openEc;
    fh.fileHandle.open(temporaryFile.filePath.c_str(),
                       boost::beast::file_mode::read, openEc);
    ASSERT_FALSE(openEc);

    HttpBody::value_type value;
    boost::system::error_code ec = boost::system::errc::make_error_code(
        boost::system::errc::permission_denied);
    ASSERT_TRUE(ec);

    value.setFd(std::move(fh), ec);

    EXPECT_FALSE(ec);
    EXPECT_TRUE(value.file().is_open());
}

} // namespace
} // namespace bmcweb
