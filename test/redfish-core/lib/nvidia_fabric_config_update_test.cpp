// SPDX-FileCopyrightText: Copyright (c) 2024 NVIDIA CORPORATION & AFFILIATES.
// All rights reserved. SPDX-License-Identifier: Apache-2.0
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "async_resp.hpp"
#include "dbus_utility.hpp"
#include "http_request.hpp"
#include "http_response.hpp"
#include "multipart_parser.hpp"
#include "nvidia_fabric_config_update.hpp"

#include <boost/asio/error.hpp>
#include <boost/beast/http/field.hpp>
#include <boost/beast/http/status.hpp>
#include <boost/beast/http/verb.hpp>
#include <boost/system/error_code.hpp>

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <system_error>
#include <vector>

#include <gtest/gtest.h>

namespace redfish
{
namespace
{

// Minimal error_category subclass used to craft error_codes with arbitrary
// message strings in tests.  Defined at namespace scope so both GCC and Clang
// agree on its destructor semantics.
class DbusTestErrorCategory final : public boost::system::error_category
{
  public:
    // Declared virtual (not 'override') so -Wnon-virtual-dtor is satisfied on
    // all compilers regardless of whether the base destructor is virtual.
    DbusTestErrorCategory() = default;
    virtual ~DbusTestErrorCategory() = default;
    DbusTestErrorCategory(const DbusTestErrorCategory&) = delete;
    DbusTestErrorCategory& operator=(const DbusTestErrorCategory&) = delete;
    DbusTestErrorCategory(DbusTestErrorCategory&&) = delete;
    DbusTestErrorCategory& operator=(DbusTestErrorCategory&&) = delete;
    const char* name() const noexcept final
    {
        return "dbus_test";
    }
    std::string message(int /*value*/) const final
    {
        return msg_;
    }
    std::string msg_;
};

// Returns an error_code whose message() contains errName.
// Uses static storage — safe for sequential gtest execution.
boost::system::error_code makeDbusError(std::string errName)
{
    static DbusTestErrorCategory cat;
    cat.msg_ = std::move(errName);
    return {1, cat};
}

// Construct a FormPart with a single Content-Disposition header.
FormPart makeFormPart(const std::string& cdValue, const std::string& content)
{
    FormPart part;
    part.fields.set("Content-Disposition", cdValue);
    part.content = content;
    return part;
}

// ---------------------------------------------------------------------------
// handleAddConfigFileError
// ---------------------------------------------------------------------------

TEST(HandleAddConfigFileError, FileAlreadyExists_Returns409)
{
    auto asyncResp = std::make_shared<bmcweb::AsyncResp>();
    handleAddConfigFileError(asyncResp, "/test/uri",
                             makeDbusError("FileAlreadyExists"));
    EXPECT_EQ(asyncResp->res.result(), boost::beast::http::status::conflict);
}

TEST(HandleAddConfigFileError, AlreadyExists_Returns409)
{
    auto asyncResp = std::make_shared<bmcweb::AsyncResp>();
    handleAddConfigFileError(asyncResp, "/test/uri",
                             makeDbusError("AlreadyExists condition"));
    EXPECT_EQ(asyncResp->res.result(), boost::beast::http::status::conflict);
}

TEST(HandleAddConfigFileError, FileEmpty_Returns400)
{
    auto asyncResp = std::make_shared<bmcweb::AsyncResp>();
    handleAddConfigFileError(asyncResp, "/test/uri",
                             makeDbusError("FileEmpty detected"));
    EXPECT_EQ(asyncResp->res.result(), boost::beast::http::status::bad_request);
}

TEST(HandleAddConfigFileError, FileTooLarge_Returns400)
{
    auto asyncResp = std::make_shared<bmcweb::AsyncResp>();
    handleAddConfigFileError(asyncResp, "/test/uri",
                             makeDbusError("FileTooLarge"));
    EXPECT_EQ(asyncResp->res.result(), boost::beast::http::status::bad_request);
}

TEST(HandleAddConfigFileError, InvalidStructure_Returns400)
{
    auto asyncResp = std::make_shared<bmcweb::AsyncResp>();
    handleAddConfigFileError(asyncResp, "/test/uri",
                             makeDbusError("InvalidStructure in binary"));
    EXPECT_EQ(asyncResp->res.result(), boost::beast::http::status::bad_request);
}

TEST(HandleAddConfigFileError, ValidationFailed_Returns400)
{
    auto asyncResp = std::make_shared<bmcweb::AsyncResp>();
    handleAddConfigFileError(asyncResp, "/test/uri",
                             makeDbusError("ValidationFailed checksum"));
    EXPECT_EQ(asyncResp->res.result(), boost::beast::http::status::bad_request);
}

TEST(HandleAddConfigFileError, UnknownError_Returns500)
{
    auto asyncResp = std::make_shared<bmcweb::AsyncResp>();
    handleAddConfigFileError(asyncResp, "/test/uri",
                             makeDbusError("SomeUnknownDBusError"));
    EXPECT_EQ(asyncResp->res.result(),
              boost::beast::http::status::internal_server_error);
}

// ---------------------------------------------------------------------------
// scanMimePartsForImportFile
// ---------------------------------------------------------------------------

TEST(ScanMimeParts, ValidImportFilePart_ReturnsContent)
{
    auto asyncResp = std::make_shared<bmcweb::AsyncResp>();
    std::vector<FormPart> parts;
    parts.push_back(
        makeFormPart(R"(form-data; name="ImportFile"; filename="switch.bin")",
                     "binary config data"));

    auto result =
        scanMimePartsForImportFile(parts, asyncResp, "fabric1", "/test/uri");
    EXPECT_EQ(result, std::make_optional<std::string>("binary config data"));
}

TEST(ScanMimeParts, MissingContentDisposition_ReturnsNullopt)
{
    auto asyncResp = std::make_shared<bmcweb::AsyncResp>();
    std::vector<FormPart> parts;
    FormPart part;
    part.content = "data";
    // No Content-Disposition header
    parts.push_back(std::move(part));

    auto result =
        scanMimePartsForImportFile(parts, asyncResp, "fabric1", "/test/uri");
    EXPECT_FALSE(result.has_value());
}

TEST(ScanMimeParts, ContentDispositionNoSemicolon_ReturnsNullopt)
{
    auto asyncResp = std::make_shared<bmcweb::AsyncResp>();
    std::vector<FormPart> parts;
    // "form-data" alone — no semicolon means no parameters can be parsed
    parts.push_back(makeFormPart("form-data", "data"));

    auto result =
        scanMimePartsForImportFile(parts, asyncResp, "fabric1", "/test/uri");
    EXPECT_FALSE(result.has_value());
}

TEST(ScanMimeParts, NoNameParameter_ReturnsNullopt)
{
    auto asyncResp = std::make_shared<bmcweb::AsyncResp>();
    std::vector<FormPart> parts;
    // Only filename=, no name=
    parts.push_back(makeFormPart("form-data; filename=\"switch.bin\"", "data"));

    auto result =
        scanMimePartsForImportFile(parts, asyncResp, "fabric1", "/test/uri");
    EXPECT_FALSE(result.has_value());
}

TEST(ScanMimeParts, UnexpectedFieldName_ReturnsNullopt)
{
    auto asyncResp = std::make_shared<bmcweb::AsyncResp>();
    std::vector<FormPart> parts;
    parts.push_back(makeFormPart(
        R"(form-data; name="WrongField"; filename="switch.bin")", "data"));

    auto result =
        scanMimePartsForImportFile(parts, asyncResp, "fabric1", "/test/uri");
    EXPECT_FALSE(result.has_value());
    EXPECT_EQ(asyncResp->res.result(), boost::beast::http::status::bad_request);
}

TEST(ScanMimeParts, DuplicateImportFile_ReturnsNullopt)
{
    auto asyncResp = std::make_shared<bmcweb::AsyncResp>();
    std::vector<FormPart> parts;
    parts.push_back(makeFormPart(
        R"(form-data; name="ImportFile"; filename="switch.bin")", "data1"));
    parts.push_back(makeFormPart(
        R"(form-data; name="ImportFile"; filename="switch2.bin")", "data2"));

    auto result =
        scanMimePartsForImportFile(parts, asyncResp, "fabric1", "/test/uri");
    EXPECT_FALSE(result.has_value());
    EXPECT_EQ(asyncResp->res.result(), boost::beast::http::status::bad_request);
}

TEST(ScanMimeParts, ImportFileMissingFilename_ReturnsNullopt)
{
    auto asyncResp = std::make_shared<bmcweb::AsyncResp>();
    std::vector<FormPart> parts;
    // name= present but no filename=
    parts.push_back(makeFormPart("form-data; name=\"ImportFile\"", "data"));

    auto result =
        scanMimePartsForImportFile(parts, asyncResp, "fabric1", "/test/uri");
    EXPECT_FALSE(result.has_value());
    EXPECT_EQ(asyncResp->res.result(), boost::beast::http::status::bad_request);
}

TEST(ScanMimeParts, NoImportFilePart_ReturnsNullopt)
{
    auto asyncResp = std::make_shared<bmcweb::AsyncResp>();
    std::vector<FormPart> parts; // empty

    auto result =
        scanMimePartsForImportFile(parts, asyncResp, "fabric1", "/test/uri");
    EXPECT_FALSE(result.has_value());
    EXPECT_EQ(asyncResp->res.result(), boost::beast::http::status::bad_request);
}

TEST(ScanMimeParts, EmptyImportFileContent_ReturnsNullopt)
{
    auto asyncResp = std::make_shared<bmcweb::AsyncResp>();
    std::vector<FormPart> parts;
    parts.push_back(
        makeFormPart(R"(form-data; name="ImportFile"; filename="switch.bin")",
                     "")); // empty content

    auto result =
        scanMimePartsForImportFile(parts, asyncResp, "fabric1", "/test/uri");
    EXPECT_FALSE(result.has_value());
}

// ---------------------------------------------------------------------------
// parseImportFilePart
// ---------------------------------------------------------------------------

TEST(ParseImportFilePart, WrongContentType_ReturnsNullopt)
{
    std::error_code ec;
    std::string body = "body content";
    crow::Request req(body, ec);
    req.addHeader("Content-Type", "application/octet-stream");

    auto asyncResp = std::make_shared<bmcweb::AsyncResp>();
    auto result = parseImportFilePart(req, asyncResp, "fabric1", "/test/uri");
    EXPECT_FALSE(result.has_value());
    EXPECT_EQ(asyncResp->res.result(), boost::beast::http::status::bad_request);
}

TEST(ParseImportFilePart, MissingContentType_ReturnsNullopt)
{
    std::error_code ec;
    std::string body = "body content";
    crow::Request req(body, ec);
    // No Content-Type header → empty string → not multipart/form-data

    auto asyncResp = std::make_shared<bmcweb::AsyncResp>();
    auto result = parseImportFilePart(req, asyncResp, "fabric1", "/test/uri");
    EXPECT_FALSE(result.has_value());
    EXPECT_EQ(asyncResp->res.result(), boost::beast::http::status::bad_request);
}

TEST(ParseImportFilePart, ValidMultipartRequest_ReturnsContent)
{
    // boundary= "testboundary123"; body uses "--testboundary123" as delimiter
    std::string body =
        "--testboundary123\r\n"
        "Content-Disposition: form-data; name=\"ImportFile\"; "
        "filename=\"switch.bin\"\r\n"
        "\r\n"
        "binarydata\r\n"
        "--testboundary123--\r\n";

    std::error_code ec;
    crow::Request req(body, ec);
    req.addHeader("Content-Type",
                  "multipart/form-data; boundary=testboundary123");

    auto asyncResp = std::make_shared<bmcweb::AsyncResp>();
    auto result = parseImportFilePart(req, asyncResp, "fabric1", "/test/uri");
    EXPECT_EQ(result, std::make_optional<std::string>("binarydata"));
}

// ---------------------------------------------------------------------------
// onAddConfigFileReply
// ---------------------------------------------------------------------------

TEST(OnAddConfigFileReply, Success_Returns204)
{
    auto asyncResp = std::make_shared<bmcweb::AsyncResp>();
    auto memfd = std::make_shared<SwitchCfgMemFd>();
    boost::system::error_code ec;

    onAddConfigFileReply(asyncResp, "fabric1", "/test/uri", memfd, ec);

    EXPECT_EQ(asyncResp->res.result(), boost::beast::http::status::no_content);
}

TEST(OnAddConfigFileReply, FileEmptyError_Returns400)
{
    auto asyncResp = std::make_shared<bmcweb::AsyncResp>();
    auto memfd = std::make_shared<SwitchCfgMemFd>();

    onAddConfigFileReply(asyncResp, "fabric1", "/test/uri", memfd,
                         makeDbusError("FileEmpty content"));

    EXPECT_EQ(asyncResp->res.result(), boost::beast::http::status::bad_request);
}

TEST(OnAddConfigFileReply, UnknownDbusError_Returns500)
{
    auto asyncResp = std::make_shared<bmcweb::AsyncResp>();
    auto memfd = std::make_shared<SwitchCfgMemFd>();

    onAddConfigFileReply(asyncResp, "fabric1", "/test/uri", memfd,
                         makeDbusError("UnexpectedDaemonError"));

    EXPECT_EQ(asyncResp->res.result(),
              boost::beast::http::status::internal_server_error);
}

// ---------------------------------------------------------------------------
// onRemoveConfigFileReply
// ---------------------------------------------------------------------------

TEST(OnRemoveConfigFileReply, Success_Returns204)
{
    auto asyncResp = std::make_shared<bmcweb::AsyncResp>();
    boost::system::error_code ec;

    onRemoveConfigFileReply(asyncResp, "fabric1", ec);

    EXPECT_EQ(asyncResp->res.result(), boost::beast::http::status::no_content);
}

TEST(OnRemoveConfigFileReply, DbusError_Returns404)
{
    auto asyncResp = std::make_shared<bmcweb::AsyncResp>();

    onRemoveConfigFileReply(asyncResp, "fabric1",
                            boost::asio::error::invalid_argument);

    EXPECT_EQ(asyncResp->res.result(), boost::beast::http::status::not_found);
}

// ---------------------------------------------------------------------------
// onDbusObjectReply
// ---------------------------------------------------------------------------

TEST(OnDbusObjectReply, ErrorCode_Returns404AndSkipsCallback)
{
    auto asyncResp = std::make_shared<bmcweb::AsyncResp>();
    bool cbCalled = false;
    SwitchCfgCallback cb = [&](const std::string&, const std::string&) {
        cbCalled = true;
    };
    dbus::utility::MapperGetObject obj;

    onDbusObjectReply(asyncResp, "fabric1", "/path/updater", "POST", cb,
                      boost::asio::error::not_found, obj);

    EXPECT_FALSE(cbCalled);
    EXPECT_EQ(asyncResp->res.result(), boost::beast::http::status::not_found);
}

TEST(OnDbusObjectReply, EmptyObjectMap_Returns404AndSkipsCallback)
{
    auto asyncResp = std::make_shared<bmcweb::AsyncResp>();
    bool cbCalled = false;
    SwitchCfgCallback cb = [&](const std::string&, const std::string&) {
        cbCalled = true;
    };
    boost::system::error_code ec;
    dbus::utility::MapperGetObject obj; // empty

    onDbusObjectReply(asyncResp, "fabric1", "/path/updater", "POST", cb, ec,
                      obj);

    EXPECT_FALSE(cbCalled);
    EXPECT_EQ(asyncResp->res.result(), boost::beast::http::status::not_found);
}

TEST(OnDbusObjectReply, ValidObject_InvokesCallbackWithServiceAndPath)
{
    auto asyncResp = std::make_shared<bmcweb::AsyncResp>();
    bool cbCalled = false;
    std::string cbSvc;
    std::string cbPath;
    SwitchCfgCallback cb =
        [&](const std::string& svc, const std::string& path) {
            cbCalled = true;
            cbSvc = svc;
            cbPath = path;
        };
    boost::system::error_code ec;
    dbus::utility::MapperGetObject obj{
        {"com.nvidia.SwitchUpdater", {"com.nvidia.SwitchConfig.Updater"}}};
    const std::string updaterPath = "/xyz/path/updater";

    onDbusObjectReply(asyncResp, "fabric1", updaterPath, "POST", cb, ec, obj);

    EXPECT_TRUE(cbCalled);
    EXPECT_EQ(cbSvc, "com.nvidia.SwitchUpdater");
    EXPECT_EQ(cbPath, updaterPath);
}

// ---------------------------------------------------------------------------
// onAssocEndPointsReply
// ---------------------------------------------------------------------------

TEST(OnAssocEndPointsReply, ErrorCode_Returns404)
{
    auto asyncResp = std::make_shared<bmcweb::AsyncResp>();
    SwitchCfgCallback cb = [](const std::string&, const std::string&) {};
    dbus::utility::MapperEndPoints eps;

    onAssocEndPointsReply(asyncResp, "fabric1",
                          "/xyz/openbmc_project/inventory/fabric1", "POST",
                          std::move(cb), boost::asio::error::not_found, eps);

    EXPECT_EQ(asyncResp->res.result(), boost::beast::http::status::not_found);
}

TEST(OnAssocEndPointsReply, EmptyEndpoints_Returns404)
{
    auto asyncResp = std::make_shared<bmcweb::AsyncResp>();
    SwitchCfgCallback cb = [](const std::string&, const std::string&) {};
    boost::system::error_code ec;
    dbus::utility::MapperEndPoints eps; // empty

    onAssocEndPointsReply(asyncResp, "fabric1",
                          "/xyz/openbmc_project/inventory/fabric1", "POST",
                          std::move(cb), ec, eps);

    EXPECT_EQ(asyncResp->res.result(), boost::beast::http::status::not_found);
}

// ---------------------------------------------------------------------------
// onFabricFoundForPost
// ---------------------------------------------------------------------------

TEST(OnFabricFoundForPost, DbusError_Returns500)
{
    auto asyncResp = std::make_shared<bmcweb::AsyncResp>();
    auto memfd = std::make_shared<SwitchCfgMemFd>();
    dbus::utility::MapperGetSubTreeResponse subtree;

    onFabricFoundForPost(asyncResp, "fabric1", "/test/uri", memfd,
                         boost::asio::error::invalid_argument, subtree);

    EXPECT_EQ(asyncResp->res.result(),
              boost::beast::http::status::internal_server_error);
}

TEST(OnFabricFoundForPost, FabricNotInSubtree_Returns404)
{
    auto asyncResp = std::make_shared<bmcweb::AsyncResp>();
    auto memfd = std::make_shared<SwitchCfgMemFd>();
    boost::system::error_code ec;
    // Subtree contains a different fabric ID
    dbus::utility::MapperGetSubTreeResponse subtree{
        {"/xyz/openbmc_project/inventory/system/fabrics/otherFabric",
         {{"service", {"iface"}}}}};

    onFabricFoundForPost(asyncResp, "fabric1", "/test/uri", memfd, ec, subtree);

    EXPECT_EQ(asyncResp->res.result(), boost::beast::http::status::not_found);
}

TEST(OnFabricFoundForPost, EmptySubtree_Returns404)
{
    auto asyncResp = std::make_shared<bmcweb::AsyncResp>();
    auto memfd = std::make_shared<SwitchCfgMemFd>();
    boost::system::error_code ec;
    dbus::utility::MapperGetSubTreeResponse subtree; // empty

    onFabricFoundForPost(asyncResp, "fabric1", "/test/uri", memfd, ec, subtree);

    EXPECT_EQ(asyncResp->res.result(), boost::beast::http::status::not_found);
}

// ---------------------------------------------------------------------------
// onFabricFoundForDelete
// ---------------------------------------------------------------------------

TEST(OnFabricFoundForDelete, DbusError_Returns500)
{
    auto asyncResp = std::make_shared<bmcweb::AsyncResp>();
    dbus::utility::MapperGetSubTreeResponse subtree;

    onFabricFoundForDelete(asyncResp, "fabric1",
                           boost::asio::error::invalid_argument, subtree);

    EXPECT_EQ(asyncResp->res.result(),
              boost::beast::http::status::internal_server_error);
}

TEST(OnFabricFoundForDelete, FabricNotInSubtree_Returns404)
{
    auto asyncResp = std::make_shared<bmcweb::AsyncResp>();
    boost::system::error_code ec;
    dbus::utility::MapperGetSubTreeResponse subtree{
        {"/xyz/openbmc_project/inventory/system/fabrics/otherFabric",
         {{"service", {"iface"}}}}};

    onFabricFoundForDelete(asyncResp, "fabric1", ec, subtree);

    EXPECT_EQ(asyncResp->res.result(), boost::beast::http::status::not_found);
}

TEST(OnFabricFoundForDelete, EmptySubtree_Returns404)
{
    auto asyncResp = std::make_shared<bmcweb::AsyncResp>();
    boost::system::error_code ec;
    dbus::utility::MapperGetSubTreeResponse subtree; // empty

    onFabricFoundForDelete(asyncResp, "fabric1", ec, subtree);

    EXPECT_EQ(asyncResp->res.result(), boost::beast::http::status::not_found);
}

} // namespace
} // namespace redfish
