// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: Copyright OpenBMC Authors
#include "async_resp.hpp"
#include "chassis.hpp"
#include "dbus_utility.hpp"
#include "generated/enums/chassis.hpp"
#include "http_response.hpp"
#include "nvidia_chassis.hpp"
#include "nvidia_platform_power_cycle.hpp"

#include <boost/beast/http/status.hpp>
#include <nlohmann/json.hpp>

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <utility>

#include <gtest/gtest.h>

namespace redfish
{
namespace
{

void assertChassisResetActionInfoGet(const std::string& chassisId,
                                     bool supportsFullPowerCycle,
                                     crow::Response& res)
{
    EXPECT_EQ(res.jsonValue["@odata.type"], "#ActionInfo.v1_1_2.ActionInfo");
    EXPECT_EQ(res.jsonValue["@odata.id"],
              "/redfish/v1/Chassis/" + chassisId + "/ResetActionInfo");
    EXPECT_EQ(res.jsonValue["Name"], "Reset Action Info");

    EXPECT_EQ(res.jsonValue["Id"], "ResetActionInfo");

    nlohmann::json::array_t parameters;
    nlohmann::json::object_t parameter;
    parameter["Name"] = "ResetType";
    parameter["Required"] = true;
    parameter["DataType"] = "String";
    nlohmann::json::array_t allowed;
    allowed.emplace_back("PowerCycle");
    if (supportsFullPowerCycle)
    {
        allowed.emplace_back("FullPowerCycle");
    }
    parameter["AllowableValues"] = std::move(allowed);
    parameters.emplace_back(std::move(parameter));

    EXPECT_EQ(res.jsonValue["Parameters"], parameters);
}

TEST(PopulateChassisResetActionInfo, StaticAttributesAreExpected)
{
    auto response = std::make_shared<bmcweb::AsyncResp>();

    std::string fakeChassis = "fakeChassis";
    response->res.setCompleteRequestHandler(
        std::bind_front(assertChassisResetActionInfoGet, fakeChassis, false));

    nvidia_chassis::populateChassisResetActionInfo(response, fakeChassis,
                                                   false);
}

TEST(PopulateChassisResetActionInfo, FullPowerCycleIsCapabilityGated)
{
    auto response = std::make_shared<bmcweb::AsyncResp>();

    std::string fakeChassis = "fakeChassis";
    response->res.setCompleteRequestHandler(
        std::bind_front(assertChassisResetActionInfoGet, fakeChassis, true));

    nvidia_chassis::populateChassisResetActionInfo(response, fakeChassis, true);
}

TEST(PopulateAuxPowerResetActionInfo, ReportsOnlyAdvertisedTypes)
{
    auto response = std::make_shared<bmcweb::AsyncResp>();
    const nvidia_platform_power_cycle::Capabilities capabilities{
        "com.nvidia.Control.Platform.PowerCycle",
        {std::string(nvidia_platform_power_cycle::auxPowerCycleForce)}};

    nvidia_platform_power_cycle::populateAuxPowerResetActionInfo(
        response, "platform", capabilities);

    const nlohmann::json& values =
        response->res.jsonValue["Parameters"][0]["AllowableValues"];
    EXPECT_EQ(values, nlohmann::json::array({"AuxPowerCycleForce"}));
}

TEST(PlatformPowerCycle, LegacyBackendAdvertisesBothAuxPowerTypes)
{
    const nvidia_platform_power_cycle::Capabilities capabilities =
        nvidia_platform_power_cycle::getLegacyAuxPowerCapabilities();

    EXPECT_TRUE(nvidia_platform_power_cycle::supports(
        capabilities, nvidia_platform_power_cycle::auxPowerCycle));
    EXPECT_TRUE(nvidia_platform_power_cycle::supports(
        capabilities, nvidia_platform_power_cycle::auxPowerCycleForce));
    EXPECT_FALSE(nvidia_platform_power_cycle::supports(
        capabilities, nvidia_platform_power_cycle::fullPowerCycle));
}

TEST(PlatformPowerCycle, MissingProviderAdvertisesLegacyAuxPowerTypes)
{
    auto response = std::make_shared<bmcweb::AsyncResp>();

    nvidia_platform_power_cycle::afterGetAuxPowerResetActionInfoCapabilities(
        response, "platform", {}, std::nullopt);

    EXPECT_EQ(response->res.result(), boost::beast::http::status::ok);
    const nlohmann::json& values =
        response->res.jsonValue["Parameters"][0]["AllowableValues"];
    EXPECT_EQ(values,
              nlohmann::json::array({"AuxPowerCycle", "AuxPowerCycleForce"}));
}

TEST(PlatformPowerCycle, MissingProviderUsesLegacyAuxBackend)
{
    EXPECT_EQ(nvidia_platform_power_cycle::resolveAuxPowerCycleBackend(
                  std::nullopt, nvidia_platform_power_cycle::auxPowerCycle),
              nvidia_platform_power_cycle::AuxPowerCycleBackend::legacy);
}

TEST(PlatformPowerCycle, AdvertisedAuxTypeUsesPlatformBackend)
{
    const nvidia_platform_power_cycle::Capabilities capabilities{
        "com.nvidia.Control.Platform.PowerCycle",
        {std::string(nvidia_platform_power_cycle::auxPowerCycle)}};

    EXPECT_EQ(nvidia_platform_power_cycle::resolveAuxPowerCycleBackend(
                  capabilities, nvidia_platform_power_cycle::auxPowerCycle),
              nvidia_platform_power_cycle::AuxPowerCycleBackend::platform);
}

TEST(PlatformPowerCycle, ProviderDoesNotFallbackForUnsupportedAuxType)
{
    const nvidia_platform_power_cycle::Capabilities capabilities{
        "com.nvidia.Control.Platform.PowerCycle",
        {std::string(nvidia_platform_power_cycle::fullPowerCycle)}};

    EXPECT_EQ(nvidia_platform_power_cycle::resolveAuxPowerCycleBackend(
                  capabilities,
                  nvidia_platform_power_cycle::auxPowerCycleForce),
              nvidia_platform_power_cycle::AuxPowerCycleBackend::unsupported);
}

TEST(TranslateChassisTypeToRedfish, TranslationsAreExpected)
{
    ASSERT_EQ(
        chassis::ChassisType::Blade,
        translateChassisTypeToRedfish(
            "xyz.openbmc_project.Inventory.Item.Chassis.ChassisType.Blade"));
    ASSERT_EQ(
        chassis::ChassisType::Component,
        translateChassisTypeToRedfish(
            "xyz.openbmc_project.Inventory.Item.Chassis.ChassisType.Component"));
    ASSERT_EQ(
        chassis::ChassisType::Enclosure,
        translateChassisTypeToRedfish(
            "xyz.openbmc_project.Inventory.Item.Chassis.ChassisType.Enclosure"));
    ASSERT_EQ(
        chassis::ChassisType::Module,
        translateChassisTypeToRedfish(
            "xyz.openbmc_project.Inventory.Item.Chassis.ChassisType.Module"));
    ASSERT_EQ(
        chassis::ChassisType::RackMount,
        translateChassisTypeToRedfish(
            "xyz.openbmc_project.Inventory.Item.Chassis.ChassisType.RackMount"));
    ASSERT_EQ(
        chassis::ChassisType::StandAlone,
        translateChassisTypeToRedfish(
            "xyz.openbmc_project.Inventory.Item.Chassis.ChassisType.StandAlone"));
    ASSERT_EQ(
        chassis::ChassisType::StorageEnclosure,
        translateChassisTypeToRedfish(
            "xyz.openbmc_project.Inventory.Item.Chassis.ChassisType.StorageEnclosure"));
    ASSERT_EQ(
        chassis::ChassisType::Zone,
        translateChassisTypeToRedfish(
            "xyz.openbmc_project.Inventory.Item.Chassis.ChassisType.Zone"));
    ASSERT_EQ(
        chassis::ChassisType::Invalid,
        translateChassisTypeToRedfish(
            "xyz.openbmc_project.Inventory.Item.Chassis.ChassisType.Unknown"));
}

TEST(HandleChassisProperties, TypeFound)
{
    auto response = std::make_shared<bmcweb::AsyncResp>();
    auto properties = dbus::utility::DBusPropertiesMap();
    properties.emplace_back(
        std::string("Type"),
        dbus::utility::DbusVariantType(
            "xyz.openbmc_project.Inventory.Item.Chassis.ChassisType.RackMount"));
    handleChassisProperties(response, properties);
    ASSERT_EQ("RackMount", response->res.jsonValue["ChassisType"]);

    response = std::make_shared<bmcweb::AsyncResp>();
    properties.clear();
    properties.emplace_back(
        std::string("Type"),
        dbus::utility::DbusVariantType(
            "xyz.openbmc_project.Inventory.Item.Chassis.ChassisType.StandAlone"));
    handleChassisProperties(response, properties);
    ASSERT_EQ("StandAlone", response->res.jsonValue["ChassisType"]);
}

TEST(HandleChassisProperties, BadTypeFound)
{
    auto response = std::make_shared<bmcweb::AsyncResp>();
    auto properties = dbus::utility::DBusPropertiesMap();
    properties.emplace_back(
        std::string("Type"),
        dbus::utility::DbusVariantType(
            "xyz.openbmc_project.Inventory.Item.Chassis.ChassisType.Unknown"));
    handleChassisProperties(response, properties);
    // We fall back to RackMount
    ASSERT_EQ("RackMount", response->res.jsonValue["ChassisType"]);
}

TEST(HandleChassisProperties, FailToGetProperty)
{
    auto response = std::make_shared<bmcweb::AsyncResp>();
    auto properties = dbus::utility::DBusPropertiesMap();
    properties.emplace_back(std::string("Type"),
                            dbus::utility::DbusVariantType(123));
    handleChassisProperties(response, properties);
    ASSERT_EQ(boost::beast::http::status::internal_server_error,
              response->res.result());
}

TEST(HandleChassisProperties, TypeNotFound)
{
    auto response = std::make_shared<bmcweb::AsyncResp>();
    auto properties = dbus::utility::DBusPropertiesMap();
    handleChassisProperties(response, properties);
    ASSERT_EQ("RackMount", response->res.jsonValue["ChassisType"]);
}

TEST(HandleDirectSKURead, NotSupportedOmitsSku)
{
    auto response = std::make_shared<bmcweb::AsyncResp>();

    nvidia_chassis_utils::handleDirectSKURead(
        response, "/xyz/openbmc_project/inventory/system/chassis/test",
        "xyz.openbmc_project.NSM", {},
        std::string(redfish::propertyNotSupported));

    EXPECT_FALSE(response->res.jsonValue.contains("SKU"));
}

TEST(HandleDirectSKURead, NotSupportedDoesNotClobberExistingSku)
{
    auto response = std::make_shared<bmcweb::AsyncResp>();
    response->res.jsonValue["SKU"] = "REAL-SKU";

    nvidia_chassis_utils::handleDirectSKURead(
        response, "/xyz/openbmc_project/inventory/system/chassis/test",
        "xyz.openbmc_project.NSM", {},
        std::string(redfish::propertyNotSupported));

    EXPECT_EQ(response->res.jsonValue["SKU"], "REAL-SKU");
}

TEST(HandleAssociatedSKURead, NotSupportedOmitsSku)
{
    auto response = std::make_shared<bmcweb::AsyncResp>();

    nvidia_chassis_utils::handleAssociatedSKURead(
        response, "xyz.openbmc_project.EntityManager",
        "/xyz/openbmc_project/inventory/system/chassis/associated", {},
        std::string(redfish::propertyNotSupported));

    EXPECT_FALSE(response->res.jsonValue.contains("SKU"));
}

TEST(HandleAssociatedSKURead, NotSupportedDoesNotClobberExistingSku)
{
    auto response = std::make_shared<bmcweb::AsyncResp>();
    response->res.jsonValue["SKU"] = "REAL-SKU";

    nvidia_chassis_utils::handleAssociatedSKURead(
        response, "xyz.openbmc_project.EntityManager",
        "/xyz/openbmc_project/inventory/system/chassis/associated", {},
        std::string(redfish::propertyNotSupported));

    EXPECT_EQ(response->res.jsonValue["SKU"], "REAL-SKU");
}

} // namespace
} // namespace redfish
