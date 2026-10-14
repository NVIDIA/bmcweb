// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION &
// AFFILIATES. All rights reserved.

#include "async_resp.hpp"
#include "dbus_utility.hpp"
#include "http_response.hpp"
#include "utils/nvidia_chassis_util.hpp"

#include <tinyxml2.h>

#include <boost/beast/http/status.hpp>
#include <boost/system/errc.hpp>
#include <boost/system/error_code.hpp>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <charconv>
#include <cstddef>
#include <filesystem>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

#include <gtest/gtest.h>

namespace redfish::nvidia_chassis_utils
{
namespace
{

constexpr const char* hostNetworkAccessPath =
    "/xyz/openbmc_project/control/host0/HostManagementNetworkAccess";

// Walk with find()/contains() rather than operator[]: on a const json a
// missing key is undefined behavior, and absence is what these tests assert.
bool hasHostNetworkAccess(const bmcweb::AsyncResp& asyncResp)
{
    const nlohmann::json& json = asyncResp.res.jsonValue;
    const auto oem = json.find("Oem");
    if (oem == json.end())
    {
        return false;
    }
    const auto nvidia = oem->find("Nvidia");
    if (nvidia == oem->end())
    {
        return false;
    }
    return nvidia->contains("HostManagementNetworkAccess");
}

// A chassis that carries no host_management_network_access association does
// not own the host-NIC control, so the property is omitted -- and that is a
// normal outcome, not a client-visible failure.

TEST(HostManagementNetworkAccess, GetWithoutAssociationOmitsProperty)
{
    auto asyncResp = std::make_shared<bmcweb::AsyncResp>();

    afterGetHostNetworkAccessEndpoints(asyncResp, boost::system::error_code{},
                                       dbus::utility::MapperEndPoints{});

    EXPECT_FALSE(hasHostNetworkAccess(*asyncResp));
    EXPECT_EQ(asyncResp->res.result(), boost::beast::http::status::ok);
}

TEST(HostManagementNetworkAccess, GetWithMapperErrorOmitsProperty)
{
    auto asyncResp = std::make_shared<bmcweb::AsyncResp>();

    afterGetHostNetworkAccessEndpoints(
        asyncResp,
        boost::system::errc::make_error_code(boost::system::errc::io_error),
        dbus::utility::MapperEndPoints{hostNetworkAccessPath});

    EXPECT_FALSE(hasHostNetworkAccess(*asyncResp));
    EXPECT_EQ(asyncResp->res.result(), boost::beast::http::status::ok);
}

// A write is different: reporting success for a property the BMC never set
// would lie to the client, so the same missing association must fail.

TEST(HostManagementNetworkAccess, PatchWithoutAssociationIsPropertyUnknown)
{
    auto asyncResp = std::make_shared<bmcweb::AsyncResp>();

    afterSetHostNetworkAccessEndpoints(asyncResp, true,
                                       boost::system::error_code{},
                                       dbus::utility::MapperEndPoints{});

    EXPECT_EQ(asyncResp->res.result(), boost::beast::http::status::bad_request);
}

TEST(HostManagementNetworkAccess, PatchWithMapperErrorIsPropertyUnknown)
{
    auto asyncResp = std::make_shared<bmcweb::AsyncResp>();

    afterSetHostNetworkAccessEndpoints(
        asyncResp, false,
        boost::system::errc::make_error_code(boost::system::errc::io_error),
        dbus::utility::MapperEndPoints{hostNetworkAccessPath});

    EXPECT_EQ(asyncResp->res.result(), boost::beast::http::status::bad_request);
}

nlohmann::json resolvedType(nlohmann::json oemNvidia)
{
    resolveNvidiaOemChassisType(oemNvidia, "/redfish/v1/Chassis/Test");
    return oemNvidia["@odata.type"];
}

TEST(ResolveNvidiaOemChassisType, SmaWithPowerSmoothing)
{
    EXPECT_EQ(resolvedType({{"LastResetReasons", nlohmann::json::array()},
                            {"ErrorInjection", nlohmann::json::object()},
                            {"PowerSmoothing", nlohmann::json::object()}}),
              "#NvidiaChassis.v1_12_0.NvidiaSMAChassis");
}

TEST(ResolveNvidiaOemChassisType, SmaWithoutPowerSmoothing)
{
    EXPECT_EQ(resolvedType({{"LastResetReasons", nlohmann::json::array()},
                            {"ErrorInjection", nlohmann::json::object()}}),
              "#NvidiaChassis.v1_12_0.NvidiaSMAChassis");
}

TEST(ResolveNvidiaOemChassisType, ReplacesTypeWrittenEarlier)
{
    EXPECT_EQ(resolvedType(
                  {{"@odata.type", "#NvidiaChassis.v1_10_0.NvidiaSMAChassis"},
                   {"LastResetReasons", nlohmann::json::array()}}),
              "#NvidiaChassis.v1_12_0.NvidiaSMAChassis");
}

TEST(ResolveNvidiaOemChassisType, RoT)
{
    EXPECT_EQ(resolvedType(
                  {{"AutomaticBackgroundCopyEnabled", false},
                   {"IrreversibleConfigEnabled", false},
                   {"RoTProtectedComponents", nlohmann::json::object()}}),
              "#NvidiaChassis.v1_12_0.NvidiaRoTChassis");
}

TEST(ResolveNvidiaOemChassisType, Cbc)
{
    EXPECT_EQ(resolvedType({{"ChassisPhysicalSlotNumber", 1},
                            {"ComputeTrayIndex", 2},
                            {"RevisionId", 3},
                            {"TopologyId", 4}}),
              "#NvidiaChassis.v1_4_0.NvidiaCBCChassis");
}

TEST(ResolveNvidiaOemChassisType, Generic)
{
    EXPECT_EQ(resolvedType({{"HardwareWriteProtectedControl", true},
                            {"StaticPowerHint", nlohmann::json::object()}}),
              "#NvidiaChassis.v1_16_0.NvidiaChassis");
}

TEST(ResolveNvidiaOemChassisType, SharedOrUnknownMembersOnlyAreGeneric)
{
    EXPECT_EQ(resolvedType({{"InbandUpdatePolicyEnabled", true}}),
              "#NvidiaChassis.v1_16_0.NvidiaChassis");
    EXPECT_EQ(resolvedType({{"NotInTheSchema", 1}}),
              "#NvidiaChassis.v1_16_0.NvidiaChassis");
    EXPECT_EQ(resolvedType(nlohmann::json::object()),
              "#NvidiaChassis.v1_16_0.NvidiaChassis");
}

TEST(ResolveNvidiaOemChassisType, DerivedTypeWinsOverGenericMembers)
{
    EXPECT_EQ(resolvedType({{"HardwareWriteProtectedControl", true},
                            {"ErrorInjection", nlohmann::json::object()}}),
              "#NvidiaChassis.v1_12_0.NvidiaSMAChassis");
}

TEST(ResolveNvidiaOemChassisType, NonObjectIsLeftAlone)
{
    nlohmann::json oemNvidia = nlohmann::json::array();
    resolveNvidiaOemChassisType(oemNvidia, "/redfish/v1/Chassis/Test");
    EXPECT_EQ(oemNvidia, nlohmann::json::array());
}

TEST(FinishChassisOemResponse, SetsTypeAndCallsNextHandler)
{
    crow::Response res;
    res.jsonValue["Oem"]["Nvidia"]["ErrorInjection"]["@odata.id"] = "x";
    bool called = false;
    finishChassisOemResponse([&called](crow::Response&) { called = true; },
                             res);
    EXPECT_TRUE(called);
    EXPECT_EQ(res.jsonValue["Oem"]["Nvidia"]["@odata.type"],
              "#NvidiaChassis.v1_12_0.NvidiaSMAChassis");
}

TEST(FinishChassisOemResponse, RemovesTypeOnlyObject)
{
    crow::Response res;
    res.jsonValue["Id"] = "Test";
    res.jsonValue["Oem"]["Nvidia"] = nlohmann::json::object();
    finishChassisOemResponse(nullptr, res);
    EXPECT_FALSE(res.jsonValue.contains("Oem"));
}

// Members of each NvidiaChassis derived type and its newest version, read
// from the CSDL that bmcweb serves.
struct CsdlType
{
    std::set<std::string> members;
    std::pair<int, int> newest{-1, -1};
    std::string newestNamespace;
};

std::pair<int, int> csdlVersion(std::string_view ns)
{
    // NvidiaChassis.v1_<minor>_<errata>
    constexpr std::pair<int, int> invalid{-1, -1};
    const size_t v = ns.find(".v1_");
    if (v == std::string_view::npos)
    {
        return invalid;
    }
    const std::string_view rest = ns.substr(v + 4);
    const size_t sep = rest.find('_');
    if (sep == std::string_view::npos)
    {
        return invalid;
    }
    const std::string_view minorStr = rest.substr(0, sep);
    const std::string_view errataStr = rest.substr(sep + 1);
    int minor = 0;
    int errata = 0;
    if (std::from_chars(minorStr.begin(), minorStr.end(), minor).ec !=
            std::errc() ||
        std::from_chars(errataStr.begin(), errataStr.end(), errata).ec !=
            std::errc())
    {
        return invalid;
    }
    return {minor, errata};
}

std::map<std::string, CsdlType> readNvidiaChassisCsdl(
    const std::filesystem::path& file)
{
    std::map<std::string, CsdlType> types;
    tinyxml2::XMLDocument doc;
    if (doc.LoadFile(file.c_str()) != tinyxml2::XML_SUCCESS)
    {
        return types;
    }
    const tinyxml2::XMLElement* edmx = doc.RootElement();
    const tinyxml2::XMLElement* services =
        edmx == nullptr ? nullptr
                        : edmx->FirstChildElement("edmx:DataServices");
    if (services == nullptr)
    {
        return types;
    }
    for (const tinyxml2::XMLElement* schema =
             services->FirstChildElement("Schema");
         schema != nullptr; schema = schema->NextSiblingElement("Schema"))
    {
        const char* ns = schema->Attribute("Namespace");
        if (ns == nullptr || csdlVersion(ns).first < 0)
        {
            continue;
        }
        for (const tinyxml2::XMLElement* type =
                 schema->FirstChildElement("ComplexType");
             type != nullptr; type = type->NextSiblingElement("ComplexType"))
        {
            const char* name = type->Attribute("Name");
            if (name == nullptr || !std::string_view(name).ends_with("Chassis"))
            {
                continue;
            }
            CsdlType& entry = types[name];
            if (csdlVersion(ns) > entry.newest)
            {
                entry.newest = csdlVersion(ns);
                entry.newestNamespace = ns;
            }
            for (const tinyxml2::XMLElement* prop = type->FirstChildElement();
                 prop != nullptr; prop = prop->NextSiblingElement())
            {
                const std::string_view tag = prop->Name();
                const char* propName = prop->Attribute("Name");
                if (propName != nullptr &&
                    (tag == "Property" || tag == "NavigationProperty"))
                {
                    entry.members.emplace(propName);
                }
            }
        }
    }
    return types;
}

constexpr std::array<std::pair<NvidiaOemChassisFamily, std::string_view>, 4>
    csdlTypeNames{{
        {NvidiaOemChassisFamily::Generic, "NvidiaChassis"},
        {NvidiaOemChassisFamily::Sma, "NvidiaSMAChassis"},
        {NvidiaOemChassisFamily::RoT, "NvidiaRoTChassis"},
        {NvidiaOemChassisFamily::Cbc, "NvidiaCBCChassis"},
    }};

TEST(NvidiaOemChassisMembers, MatchCsdl)
{
    const std::filesystem::path csdl =
        std::filesystem::path(__FILE__).parent_path() /
        "../../../../redfish-core/schema/oem/nvidia/csdl/NvidiaChassis_v1.xml";
    std::error_code ec;
    if (!std::filesystem::exists(csdl, ec))
    {
        // Only available when run from a source tree, e.g. meson test.
        GTEST_SKIP() << "CSDL not found: " << csdl;
    }
    const std::map<std::string, CsdlType> types = readNvidiaChassisCsdl(csdl);

    for (const auto& [family, typeName] : csdlTypeNames)
    {
        auto type = types.find(std::string(typeName));
        ASSERT_NE(type, types.end()) << typeName;

        // The emitted type is the newest version of the derived type.
        EXPECT_EQ(nvidiaOemChassisType(family),
                  "#" + type->second.newestNamespace + "." +
                      std::string(typeName));

        // Every member the CSDL declares on this type is mapped to it, or is
        // declared by more than one type.
        for (const std::string& name : type->second.members)
        {
            if (std::ranges::find(nvidiaOemChassisSharedMembers, name) !=
                nvidiaOemChassisSharedMembers.end())
            {
                continue;
            }
            const auto* member = std::ranges::find(
                nvidiaOemChassisMembers, name, &NvidiaOemChassisMember::name);
            ASSERT_NE(member, nvidiaOemChassisMembers.end())
                << typeName << "." << name;
            EXPECT_EQ(member->family, family) << typeName << "." << name;
        }
    }

    // No table entry names a member the CSDL does not declare on its type.
    for (const NvidiaOemChassisMember& member : nvidiaOemChassisMembers)
    {
        const auto* typeName = std::ranges::find(
            csdlTypeNames, member.family,
            &std::pair<NvidiaOemChassisFamily, std::string_view>::first);
        ASSERT_NE(typeName, csdlTypeNames.end());
        auto type = types.find(std::string(typeName->second));
        ASSERT_NE(type, types.end());
        EXPECT_TRUE(type->second.members.contains(std::string(member.name)))
            << typeName->second << "." << member.name;
    }

    // Shared members really are declared by more than one type.
    for (std::string_view shared : nvidiaOemChassisSharedMembers)
    {
        int declaredBy = 0;
        for (const auto& entry : types)
        {
            declaredBy +=
                entry.second.members.contains(std::string(shared)) ? 1 : 0;
        }
        EXPECT_GT(declaredBy, 1) << shared;
    }
}

} // namespace
} // namespace redfish::nvidia_chassis_utils
