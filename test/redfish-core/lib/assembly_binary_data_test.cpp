// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: Copyright OpenBMC Authors
#include "assembly_binary_data.hpp"
#include "async_resp.hpp"
#include "dbus_utility.hpp"
#include "http_response.hpp"
#include "utils/assembly_utils.hpp"

#include <boost/beast/http/status.hpp>
#include <boost/system/errc.hpp>
#include <boost/system/linux_error.hpp>
#include <nlohmann/json.hpp>

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

namespace redfish::assembly_binary_data
{
namespace
{

constexpr const char* assemblyRoot =
    "/xyz/openbmc_project/inventory/system/assembly/";

TEST(AssemblyBinaryData, GetAssemblyMemberId)
{
    using assembly_utils::getAssemblyMemberId;
    EXPECT_EQ(getAssemblyMemberId(3, {}), "3");
    EXPECT_EQ(getAssemblyMemberId(3, {{"Name", std::string("x")}}), "3");
    EXPECT_EQ(getAssemblyMemberId(3, {{"AssemblyID", uint64_t{7}}}), "7");
    EXPECT_EQ(getAssemblyMemberId(3, {{"AssemblyID", std::string("7")}}), "3");
    // GetAll on all interfaces may return AssemblyID more than once.
    EXPECT_EQ(getAssemblyMemberId(3, {{"AssemblyID", uint64_t{7}},
                                      {"AssemblyID", uint64_t{8}}}),
              "8");
}

TEST(AssemblyBinaryData, GetAllAssemblyPropertiesNoService)
{
    bool called = false;
    assembly_utils::getAllAssemblyProperties(
        std::string(assemblyRoot) + "A/Assembly1", {},
        [&called](const boost::system::error_code& ec,
                  const dbus::utility::DBusPropertiesMap& properties) {
            called = true;
            EXPECT_EQ(ec.value(),
                      boost::system::linux_error::bad_request_descriptor);
            EXPECT_TRUE(properties.empty());
        });
    EXPECT_TRUE(called);
}

std::string messageId(const crow::Response& res)
{
    const nlohmann::json::json_pointer ptr(
        "/error/@Message.ExtendedInfo/0/MessageId");
    if (!res.jsonValue.contains(ptr) || !res.jsonValue[ptr].is_string())
    {
        return "";
    }
    return res.jsonValue[ptr].get<std::string>();
}

boost::system::error_code errorCode(boost::system::errc::errc_t value)
{
    return boost::system::errc::make_error_code(value);
}

TEST(AssemblyBinaryData, HandleBinaryDataError)
{
    struct Case
    {
        std::string_view dbusError;
        std::string_view action;
        boost::beast::http::status status;
        std::string_view messageId;
    };
    const std::vector<Case> cases = {
        {"xyz.openbmc_project.Common.Error.InvalidArgument", "",
         boost::beast::http::status::bad_request, ".UnrecognizedRequestBody"},
        {"xyz.openbmc_project.Common.Error.NotAllowed", "",
         boost::beast::http::status::method_not_allowed,
         ".OperationNotAllowed"},
        {"xyz.openbmc_project.Common.Error.NotAllowed", resetToDefaultsAction,
         boost::beast::http::status::bad_request, ".ActionNotSupported"},
        {"xyz.openbmc_project.Common.Error.Unavailable", "",
         boost::beast::http::status::service_unavailable,
         ".ServiceTemporarilyUnavailable"},
        {"org.freedesktop.DBus.Error.ServiceUnknown", "",
         boost::beast::http::status::service_unavailable,
         ".ServiceTemporarilyUnavailable"},
        {"org.freedesktop.DBus.Error.NoReply", resetToDefaultsAction,
         boost::beast::http::status::service_unavailable,
         ".ServiceTemporarilyUnavailable"},
        {"xyz.openbmc_project.Common.Device.Error.WriteFailure", "",
         boost::beast::http::status::internal_server_error, ".InternalError"},
    };
    for (const Case& c : cases)
    {
        SCOPED_TRACE(c.dbusError);
        crow::Response res;
        handleBinaryDataError(res, "/binary_data/A",
                              errorCode(boost::system::errc::io_error),
                              c.dbusError, c.action);
        EXPECT_EQ(res.result(), c.status);
        EXPECT_THAT(messageId(res), testing::EndsWith(c.messageId));
    }
}

TEST(AssemblyBinaryData, FindBinaryDataObject)
{
    const dbus::utility::MapperGetSubTreeResponse subtree = {
        {"/xyz/openbmc_project/binary_data/A_Assembly1",
         {{"svc.A", {binaryDataInterface}}}}};

    EXPECT_FALSE(findBinaryDataObject(errorCode(boost::system::errc::io_error),
                                      subtree));
    EXPECT_FALSE(findBinaryDataObject({}, {}));
    EXPECT_FALSE(findBinaryDataObject(
        {}, {{"/xyz/openbmc_project/binary_data/A_Assembly1", {}}}));

    std::optional<BinaryDataObject> object = findBinaryDataObject({}, subtree);
    ASSERT_TRUE(object.has_value());
    const BinaryDataObject found = object.value_or(BinaryDataObject{});
    EXPECT_EQ(found.path, "/xyz/openbmc_project/binary_data/A_Assembly1");
    EXPECT_EQ(found.service, "svc.A");
}

TEST(AssemblyBinaryData, AfterGetBinaryDataObject)
{
    const nlohmann::json::json_pointer ptr("/Assemblies/0");
    const std::string path = std::string(assemblyRoot) + "A/Assembly1";
    auto asyncResp = std::make_shared<bmcweb::AsyncResp>();
    afterGetBinaryDataObject(asyncResp, "Chassis_0", path, ptr, std::nullopt);
    EXPECT_FALSE(asyncResp->res.jsonValue.contains(ptr));
    EXPECT_FALSE(asyncResp->res.jsonValue.contains("Actions"));

    afterGetBinaryDataObject(asyncResp, "Chassis_0", path, ptr,
                             BinaryDataObject{"/binary_data/A", "svc.A"});
    EXPECT_EQ(asyncResp->res.jsonValue[ptr]["BinaryDataURI"],
              "/redfish/v1/Chassis/Chassis_0/Assembly/Assembly1/BinaryData");
}

TEST(AssemblyBinaryData, AfterGetValidChassis)
{
    bool called = false;
    const std::function<void()> callback = [&called]() { called = true; };

    auto unknown = std::make_shared<bmcweb::AsyncResp>();
    afterGetValidChassis(unknown, "Chassis_9", callback, std::nullopt);
    EXPECT_EQ(unknown->res.result(), boost::beast::http::status::not_found);
    EXPECT_THAT(messageId(unknown->res),
                testing::EndsWith(".ResourceNotFound"));
    EXPECT_FALSE(called);

    auto known = std::make_shared<bmcweb::AsyncResp>();
    afterGetValidChassis(known, "Chassis_0", callback,
                         "/xyz/openbmc_project/inventory/system/chassis/"
                         "Chassis_0");
    EXPECT_TRUE(called);
}

TEST(AssemblyBinaryData, AfterGetAssembliesForResolve)
{
    bool called = false;
    const ResolveCallback callback = [&called](const BinaryDataObject&) {
        called = true;
    };

    auto notFound = std::make_shared<bmcweb::AsyncResp>();
    afterGetAssembliesForResolve(notFound, "Chassis_0", "Assembly1", callback,
                                 {},
                                 {std::string(assemblyRoot) + "A/Assembly0"});
    EXPECT_EQ(notFound->res.result(), boost::beast::http::status::not_found);

    auto ambiguous = std::make_shared<bmcweb::AsyncResp>();
    afterGetAssembliesForResolve(
        ambiguous, "Chassis_0", "Assembly1", callback, {},
        {std::string(assemblyRoot) + "A/Assembly1",
         std::string(assemblyRoot) + "B/Assembly1"});
    EXPECT_EQ(ambiguous->res.result(),
              boost::beast::http::status::internal_server_error);
    EXPECT_FALSE(called);
}

TEST(AssemblyBinaryData, ResettableScanNoAssemblies)
{
    bool called = false;
    std::make_shared<ResettableScan>(
        std::vector<std::string>{},
        [&called](bool anyBinaryData,
                  const std::vector<BinaryDataMember>& resettable) {
            called = true;
            EXPECT_FALSE(anyBinaryData);
            EXPECT_TRUE(resettable.empty());
        })
        ->run();
    EXPECT_TRUE(called);
}

TEST(AssemblyBinaryData, AfterGetMembersForActionInfo)
{
    auto noBinaryData = std::make_shared<bmcweb::AsyncResp>();
    afterGetMembersForActionInfo(noBinaryData, "Chassis_0", false, {});
    EXPECT_EQ(noBinaryData->res.result(),
              boost::beast::http::status::not_found);
    EXPECT_THAT(messageId(noBinaryData->res),
                testing::EndsWith(".ResourceNotFound"));

    // The action is advertised, so its ActionInfo exists, listing nothing.
    auto noResettable = std::make_shared<bmcweb::AsyncResp>();
    afterGetMembersForActionInfo(noResettable, "Chassis_0", true, {});
    EXPECT_EQ(noResettable->res.result(), boost::beast::http::status::ok);
    EXPECT_EQ(noResettable->res.jsonValue["Parameters"][0]["AllowableValues"],
              nlohmann::json::array());

    auto asyncResp = std::make_shared<bmcweb::AsyncResp>();
    afterGetMembersForActionInfo(asyncResp, "Chassis_0", true,
                                 {{"1", {"/b", "svc"}}, {"3", {"/d", "svc"}}});
    EXPECT_EQ(asyncResp->res.result(), boost::beast::http::status::ok);
    EXPECT_THAT(asyncResp->res.getHeaderValue("Link"),
                testing::HasSubstr("rel=describedby"));
    const nlohmann::json& json = asyncResp->res.jsonValue;
    EXPECT_EQ(json["@odata.type"], "#ActionInfo.v1_1_2.ActionInfo");
    EXPECT_EQ(json["@odata.id"], "/redfish/v1/Chassis/Chassis_0/Assembly/Oem/"
                                 "Nvidia/ResetToDefaultsActionInfo");
    EXPECT_EQ(json["Id"], "ResetToDefaultsActionInfo");
    ASSERT_EQ(json["Parameters"].size(), 1U);
    const nlohmann::json& parameter = json["Parameters"][0];
    EXPECT_EQ(parameter["Name"], "MemberId");
    EXPECT_EQ(parameter["Required"], true);
    EXPECT_EQ(parameter["DataType"], "String");
    EXPECT_EQ(parameter["AllowableValues"], nlohmann::json({"1", "3"}));
}

TEST(AssemblyBinaryData, AfterGetMembersForReset)
{
    auto noMembers = std::make_shared<bmcweb::AsyncResp>();
    afterGetMembersForReset(noMembers, "1", true, {});
    EXPECT_EQ(noMembers->res.result(), boost::beast::http::status::bad_request);
    EXPECT_THAT(messageId(noMembers->res),
                testing::EndsWith(".ActionNotSupported"));

    auto duplicateId = std::make_shared<bmcweb::AsyncResp>();
    afterGetMembersForReset(duplicateId, "1", true,
                            {{"1", {"/b", "svc"}}, {"1", {"/c", "svc"}}});
    EXPECT_EQ(duplicateId->res.result(),
              boost::beast::http::status::internal_server_error);

    auto unknownId = std::make_shared<bmcweb::AsyncResp>();
    afterGetMembersForReset(unknownId, "2", true, {{"1", {"/b", "svc"}}});
    EXPECT_EQ(unknownId->res.result(), boost::beast::http::status::bad_request);
    EXPECT_THAT(messageId(unknownId->res),
                testing::EndsWith(".ActionParameterValueNotInList"));
}

} // namespace
} // namespace redfish::assembly_binary_data
