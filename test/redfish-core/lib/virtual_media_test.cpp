// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: Copyright OpenBMC Authors

#include "async_resp.hpp"
#include "dbus_utility.hpp"
#include "http_response.hpp"
#include "utils/json_utils.hpp"
#include "virtual_media.hpp"

#include <boost/beast/http/status.hpp>
#include <nlohmann/json.hpp>
#include <sdbusplus/message/native_types.hpp>

#include <memory>
#include <optional>
#include <string>

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

dbus::utility::DBusInterfacesMap process(bool active)
{
    return {{"xyz.openbmc_project.VirtualMedia.Process", {{"Active", active}}}};
}

// Rejected writes must be answered before attempting to use the system bus.
TEST(VirtualMediaPatch, MountedLegacyRejectsWrite)
{
    auto resp = std::make_shared<bmcweb::AsyncResp>();
    doSetVerifyCertificate(resp, "test.service",
                           sdbusplus::message::object_path(
                               "/xyz/openbmc_project/VirtualMedia/Legacy/USB1"),
                           true, process(true));
    EXPECT_TRUE(messageId(resp->res).ends_with(".ResourceInUse"));
}

TEST(VirtualMediaPatch, MissingActiveRejectsWrite)
{
    auto resp = std::make_shared<bmcweb::AsyncResp>();
    doSetVerifyCertificate(resp, "test.service",
                           sdbusplus::message::object_path(
                               "/xyz/openbmc_project/VirtualMedia/Legacy/USB1"),
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
    doSetVerifyCertificate(resp, "test.service",
                           sdbusplus::message::object_path(
                               "/xyz/openbmc_project/VirtualMedia/Legacy/USB1"),
                           false, interfaces);
    EXPECT_EQ(resp->res.result(),
              boost::beast::http::status::internal_server_error);
}

TEST(VirtualMediaPatch, ProxyRejectsWrite)
{
    auto resp = std::make_shared<bmcweb::AsyncResp>();
    dbus::utility::ManagedObjectType objects = {
        {sdbusplus::message::object_path(
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
        {sdbusplus::message::object_path(
             "/xyz/openbmc_project/VirtualMedia/Legacy/USB1"),
         process(false)}};
    afterGetVerifyCertificateObjects(resp, "test.service", "USB1", std::nullopt,
                                     {}, objects);
    EXPECT_EQ(resp->res.result(), boost::beast::http::status::no_content);
}

TEST(VirtualMediaPatch, LegacyWinsRegardlessOfObjectOrder)
{
    const dbus::utility::ManagedObjectType::value_type legacy = {
        sdbusplus::message::object_path(
            "/xyz/openbmc_project/VirtualMedia/Legacy/USB1"),
        process(true)};
    const dbus::utility::ManagedObjectType::value_type proxy = {
        sdbusplus::message::object_path(
            "/xyz/openbmc_project/VirtualMedia/Proxy/USB1"),
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
    vmParseInterfaceObject(interfaces, resp);
    EXPECT_FALSE(resp->res.jsonValue.contains("VerifyCertificate"));
    EXPECT_EQ(resp->res.jsonValue["Inserted"], true);
    EXPECT_EQ(resp->res.result(), boost::beast::http::status::ok);
}

TEST(VirtualMediaGet, MissingCertificateIsOmitted)
{
    auto resp = std::make_shared<bmcweb::AsyncResp>();
    resp->res.jsonValue = vmItemTemplate("BMC_0", "USB1");
    vmParseInterfaceObject(process(false), resp);
    EXPECT_FALSE(resp->res.jsonValue.contains("VerifyCertificate"));
    EXPECT_EQ(resp->res.jsonValue["@odata.type"],
              "#VirtualMedia.v1_4_0.VirtualMedia");
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
