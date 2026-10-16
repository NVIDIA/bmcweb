// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: Copyright OpenBMC Authors

#include "async_resp.hpp"
#include "dbus_singleton.hpp"
#include "dbus_utility.hpp"
#include "http_response.hpp"
#include "utils/json_utils.hpp"
#include "virtual_media.hpp"

#include <boost/asio/error.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/beast/http/status.hpp>
#include <nlohmann/json.hpp>
#include <sdbusplus/asio/connection.hpp>
#include <sdbusplus/asio/object_server.hpp>
#include <sdbusplus/message/native_types.hpp>

#include <chrono>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <variant>

#include <gtest/gtest.h>

namespace redfish
{
namespace
{

std::string messageId(const crow::Response& res)
{
    if (res.jsonValue.contains("error"))
    {
        return res.jsonValue.at("error")
            .at("@Message.ExtendedInfo")
            .at(0)
            .at("MessageId")
            .get<std::string>();
    }
    return res.jsonValue.at("VerifyCertificate@Message.ExtendedInfo")
        .at(0)
        .at("MessageId")
        .get<std::string>();
}

const nlohmann::json& extendedInfo(const crow::Response& res)
{
    return res.jsonValue.at("error").at("@Message.ExtendedInfo");
}

dbus::utility::DBusInterfacesMap process(bool active)
{
    return {{"xyz.openbmc_project.VirtualMedia.Process", {{"Active", active}}}};
}

// Rejected writes must be answered before attempting to use the system bus.
TEST(VirtualMediaPatch, MountedLegacyRejectsWrite)
{
    auto resp = std::make_shared<bmcweb::AsyncResp>();
    doSetVerifyCertificate(
        resp, "test.service",
        sdbusplus::object_path("/xyz/openbmc_project/VirtualMedia/Legacy/USB1"),
        true, process(true));
    EXPECT_TRUE(messageId(resp->res).ends_with(".ResourceInUse"));
}

TEST(VirtualMediaPatch, MissingActiveRejectsWrite)
{
    auto resp = std::make_shared<bmcweb::AsyncResp>();
    doSetVerifyCertificate(
        resp, "test.service",
        sdbusplus::object_path("/xyz/openbmc_project/VirtualMedia/Legacy/USB1"),
        true, {});
    EXPECT_EQ(resp->res.result(),
              boost::beast::http::status::internal_server_error);
}

TEST(VirtualMediaPatch, WrongActiveTypeRejectsWrite)
{
    auto resp = std::make_shared<bmcweb::AsyncResp>();
    dbus::utility::DBusInterfacesMap interfaces = {
        {"xyz.openbmc_project.VirtualMedia.Process",
         {{"Active", std::string("false")}}}};
    doSetVerifyCertificate(
        resp, "test.service",
        sdbusplus::object_path("/xyz/openbmc_project/VirtualMedia/Legacy/USB1"),
        false, interfaces);
    EXPECT_EQ(resp->res.result(),
              boost::beast::http::status::internal_server_error);
}

TEST(VirtualMediaPatch, ProxyRejectsWrite)
{
    auto resp = std::make_shared<bmcweb::AsyncResp>();
    dbus::utility::ManagedObjectType objects = {
        {sdbusplus::object_path(
             "/xyz/openbmc_project/VirtualMedia/Proxy/Slot_0"),
         process(false)}};
    afterGetVerifyCertificateObjects(resp, "test.service", "Slot_0", true, {},
                                     objects);
    EXPECT_TRUE(messageId(resp->res).ends_with(".PropertyNotWritable"));
}

TEST(VirtualMediaPatch, MissingResourceReturnsNotFound)
{
    auto resp = std::make_shared<bmcweb::AsyncResp>();
    afterGetVerifyCertificateObjects(resp, "test.service", "missing", true, {},
                                     {});
    EXPECT_EQ(resp->res.result(), boost::beast::http::status::not_found);
}

TEST(VirtualMediaPatch, NoOpStillValidatesResource)
{
    auto resp = std::make_shared<bmcweb::AsyncResp>();
    afterGetVerifyCertificateObjects(resp, "test.service", "missing",
                                     std::nullopt, {}, {});
    EXPECT_EQ(resp->res.result(), boost::beast::http::status::not_found);
}

TEST(VirtualMediaPatch, ExistingNoOpReturnsNoContent)
{
    auto resp = std::make_shared<bmcweb::AsyncResp>();
    dbus::utility::ManagedObjectType objects = {
        {sdbusplus::object_path(
             "/xyz/openbmc_project/VirtualMedia/Legacy/USB1"),
         process(false)}};
    afterGetVerifyCertificateObjects(resp, "test.service", "USB1", std::nullopt,
                                     {}, objects);
    EXPECT_EQ(resp->res.result(), boost::beast::http::status::no_content);
}

TEST(VirtualMediaPatch, LegacyWinsRegardlessOfObjectOrder)
{
    const dbus::utility::ManagedObjectType::value_type legacy = {
        sdbusplus::object_path("/xyz/openbmc_project/VirtualMedia/Legacy/USB1"),
        process(true)};
    const dbus::utility::ManagedObjectType::value_type proxy = {
        sdbusplus::object_path("/xyz/openbmc_project/VirtualMedia/Proxy/USB1"),
        process(false)};
    for (const auto& objects :
         {dbus::utility::ManagedObjectType{proxy, legacy},
          dbus::utility::ManagedObjectType{legacy, proxy}})
    {
        auto resp = std::make_shared<bmcweb::AsyncResp>();
        afterGetVerifyCertificateObjects(resp, "test.service", "USB1", false,
                                         {}, objects);
        EXPECT_TRUE(messageId(resp->res).ends_with(".ResourceInUse"));
    }
}

TEST(VirtualMediaGet, BadCertificateTypeDoesNotHideInserted)
{
    auto resp = std::make_shared<bmcweb::AsyncResp>();
    dbus::utility::DBusInterfacesMap interfaces = {
        {"xyz.openbmc_project.VirtualMedia.MountPoint",
         {{"VerifyCertificate", std::string("bad")}}},
        {"xyz.openbmc_project.VirtualMedia.Process", {{"Active", true}}}};
    vmParseInterfaceObject(interfaces, resp, VmMode::Legacy);
    EXPECT_FALSE(resp->res.jsonValue.contains("VerifyCertificate"));
    EXPECT_EQ(resp->res.jsonValue["Inserted"], true);
    EXPECT_EQ(resp->res.result(), boost::beast::http::status::ok);
}

TEST(VirtualMediaGet, MissingCertificateIsOmitted)
{
    auto resp = std::make_shared<bmcweb::AsyncResp>();
    resp->res.jsonValue = vmItemTemplate("BMC_0", "USB1");
    vmParseInterfaceObject(process(false), resp, VmMode::Legacy);
    EXPECT_FALSE(resp->res.jsonValue.contains("VerifyCertificate"));
    EXPECT_EQ(resp->res.jsonValue["@odata.type"],
              "#VirtualMedia.v1_4_0.VirtualMedia");
}

TEST(InsertMediaLastError, UnavailableLastErrorReturnsInternalError)
{
    auto resp = std::make_shared<bmcweb::AsyncResp>();
    boost::system::error_code ec = boost::asio::error::invalid_argument;
    applyInsertMediaLastError(resp, false, ec, "ignored when the read fails");

    EXPECT_EQ(resp->res.result(),
              boost::beast::http::status::internal_server_error);
    const nlohmann::json& info = extendedInfo(resp->res);
    ASSERT_EQ(info.size(), 1);
    EXPECT_TRUE(info.at(0)
                    .at("MessageId")
                    .get<std::string>()
                    .ends_with(".InternalError"));
}

TEST(InsertMediaLastError, EmptyLastErrorReturnsInternalError)
{
    auto resp = std::make_shared<bmcweb::AsyncResp>();
    applyInsertMediaLastError(resp, false, {}, "");

    EXPECT_EQ(resp->res.result(),
              boost::beast::http::status::internal_server_error);
    EXPECT_TRUE(messageId(resp->res).ends_with(".InternalError"));
}

TEST(InsertMediaLastError, InvalidArgumentWithoutLastError)
{
    auto resp = std::make_shared<bmcweb::AsyncResp>();
    applyInsertMediaLastError(resp, true, {}, "");

    EXPECT_EQ(resp->res.result(), boost::beast::http::status::bad_request);
    const nlohmann::json& info = extendedInfo(resp->res);
    ASSERT_EQ(info.size(), 1);
    EXPECT_TRUE(info.at(0)
                    .at("MessageId")
                    .get<std::string>()
                    .ends_with(".ActionParameterValueFormatError"));
    EXPECT_EQ(info.at(0).at("MessageArgs").at(1), "UserName or Password");
    EXPECT_EQ(info.at(0).at("MessageArgs").at(2), "VirtualMedia.InsertMedia");
}

TEST(InsertMediaLastError, InvalidArgumentAppendsLastError)
{
    auto resp = std::make_shared<bmcweb::AsyncResp>();
    applyInsertMediaLastError(resp, true, {}, "certificate rejected");

    EXPECT_EQ(resp->res.result(), boost::beast::http::status::bad_request);
    const nlohmann::json& info = extendedInfo(resp->res);
    ASSERT_EQ(info.size(), 2);
    EXPECT_TRUE(info.at(0)
                    .at("MessageId")
                    .get<std::string>()
                    .ends_with(".ActionParameterValueFormatError"));
    EXPECT_EQ(info.at(1).at("MessageId"),
              "ResourceEvent.1.0.3.ResourceErrorsDetected");
    EXPECT_EQ(info.at(1).at("MessageArgs").at(0), "InsertMedia");
    EXPECT_EQ(info.at(1).at("MessageArgs").at(1), "certificate rejected");
}

TEST(InsertMediaLastError, LastErrorIsTheFailureResponse)
{
    auto resp = std::make_shared<bmcweb::AsyncResp>();
    applyInsertMediaLastError(resp, false, {}, "certificate rejected");

    EXPECT_EQ(resp->res.result(),
              boost::beast::http::status::internal_server_error);
    const nlohmann::json& info = extendedInfo(resp->res);
    ASSERT_EQ(info.size(), 1);
    EXPECT_EQ(info.at(0).at("MessageId"),
              "ResourceEvent.1.0.3.ResourceErrorsDetected");
    EXPECT_EQ(info.at(0).at("MessageArgs").at(1), "certificate rejected");
}

// Drives doMountVmLegacy through D-Bus. A non-sdbusplus exception from Mount
// is returned as InvalidArgs, which that callback maps to invalidArgument.
class MountVmLegacyBus : public ::testing::Test
{
  protected:
    static constexpr const char* objectPath =
        "/xyz/openbmc_project/VirtualMedia/Legacy/USB1";

    boost::asio::io_context ioc;
    std::string serviceName;
    std::shared_ptr<sdbusplus::asio::connection> conn;
    std::unique_ptr<sdbusplus::asio::object_server> server;

    void SetUp() override
    {
        try
        {
            static int nextService = 0;
            serviceName = "xyz.openbmc_project.VirtualMedia.BmcwebInsertTest" +
                          std::to_string(++nextService);
            conn = std::make_shared<sdbusplus::asio::connection>(ioc);
            server = std::make_unique<sdbusplus::asio::object_server>(conn);
            conn->request_name(serviceName.c_str());
        }
        catch (const std::exception& e)
        {
            GTEST_SKIP() << "D-Bus not available: " << e.what();
        }
        crow::connections::systemBus = conn.get();
    }

    void TearDown() override
    {
        ioc.stop();
        crow::connections::systemBus = nullptr;
        server.reset();
        conn.reset();
    }

    using MountFd = std::variant<int32_t, sdbusplus::message::unix_fd>;

    std::shared_ptr<bmcweb::AsyncResp> mount(bool invalidArgument)
    {
        auto legacy = server->add_interface(
            objectPath, "xyz.openbmc_project.VirtualMedia.Legacy");
        // A non-sdbusplus exception is returned as InvalidArgs. The flag is
        // captured so this function is not implicitly noreturn.
        bool registered = legacy->register_method(
            "Mount", [invalidArgument](std::string, bool, MountFd) -> bool {
                if (invalidArgument)
                {
                    throw std::runtime_error("rejected");
                }
                return false;
            });
        auto resp = std::make_shared<bmcweb::AsyncResp>();
        auto mountPoint = server->add_interface(
            objectPath, "xyz.openbmc_project.VirtualMedia.MountPoint");
        if (!registered || !legacy->initialize() ||
            !mountPoint->register_property(
                "LastError", std::string("certificate rejected")) ||
            !mountPoint->initialize())
        {
            ADD_FAILURE() << "failed to publish the virtual media object";
            return resp;
        }

        doMountVmLegacy(resp, serviceName, "USB1", "https://example/image.iso",
                        true, std::string{}, std::string{});

        const auto deadline =
            std::chrono::steady_clock::now() + std::chrono::seconds(2);
        while (resp->res.result() == boost::beast::http::status::ok &&
               std::chrono::steady_clock::now() < deadline)
        {
            ioc.run_for(std::chrono::milliseconds(20));
            ioc.restart();
        }
        return resp;
    }
};

TEST_F(MountVmLegacyBus, InvalidArgumentForwardsLastError)
{
    auto resp = mount(true);

    ASSERT_TRUE(resp->res.jsonValue.contains("error"));
    EXPECT_EQ(resp->res.result(), boost::beast::http::status::bad_request);
    const nlohmann::json& info = extendedInfo(resp->res);
    ASSERT_EQ(info.size(), 2);
    EXPECT_TRUE(info.at(0)
                    .at("MessageId")
                    .get<std::string>()
                    .ends_with(".ActionParameterValueFormatError"));
    EXPECT_EQ(info.at(1).at("MessageId"),
              "ResourceEvent.1.0.3.ResourceErrorsDetected");
    EXPECT_EQ(info.at(1).at("MessageArgs").at(1), "certificate rejected");
}

TEST_F(MountVmLegacyBus, OtherFailureUsesLastErrorOnly)
{
    auto resp = mount(false);

    ASSERT_TRUE(resp->res.jsonValue.contains("error"));
    EXPECT_EQ(resp->res.result(),
              boost::beast::http::status::internal_server_error);
    const nlohmann::json& info = extendedInfo(resp->res);
    ASSERT_EQ(info.size(), 1);
    EXPECT_EQ(info.at(0).at("MessageId"),
              "ResourceEvent.1.0.3.ResourceErrorsDetected");
    EXPECT_EQ(info.at(0).at("MessageArgs").at(1), "certificate rejected");
}

TEST(VirtualMediaPatch, NullIsRejectedAndFalseIsPreserved)
{
    for (const auto& value :
         {nlohmann::json(nullptr), nlohmann::json(false), nlohmann::json(true)})
    {
        crow::Response res;
        std::optional<bool> certificate;
        nlohmann::json::object_t body = {{"VerifyCertificate", value}};
        bool valid = json_util::readJsonObject(body, res, "VerifyCertificate",
                                               certificate);
        if (value.is_null())
        {
            EXPECT_FALSE(valid);
            EXPECT_TRUE(messageId(res).ends_with(".PropertyValueTypeError"));
        }
        else
        {
            ASSERT_TRUE(valid);
            EXPECT_EQ(certificate, value.get<bool>());
        }
    }
}

} // namespace
} // namespace redfish
