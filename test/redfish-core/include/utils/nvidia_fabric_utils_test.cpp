// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES.
// All rights reserved. SPDX-License-Identifier: Apache-2.0

#include "utils/nvidia_fabric_utils.hpp"

#include <boost/beast/http/status.hpp>
#include <boost/system/error_code.hpp>

#include <memory>
#include <string>
#include <vector>

#include <gtest/gtest.h>

namespace redfish::nvidia_fabric_utils
{
namespace
{

const std::string enumPrefix =
    "com.nvidia.DeviceMode.PowerCappingMode.PowerCapMode.";

TEST(PowerCapModeConversion, DbusToRedfish)
{
    EXPECT_EQ(translatePowerCapModeDbusToRedfish(enumPrefix + "Default"),
              "Default");
    EXPECT_EQ(translatePowerCapModeDbusToRedfish(enumPrefix + "Enabled"),
              "Enabled");
    EXPECT_EQ(translatePowerCapModeDbusToRedfish(enumPrefix + "Disabled"),
              "Disabled");
    EXPECT_TRUE(
        translatePowerCapModeDbusToRedfish(enumPrefix + "Invalid").empty());
    EXPECT_TRUE(translatePowerCapModeDbusToRedfish("Enabled").empty());
}

TEST(PowerCapModeConversion, RedfishToDbus)
{
    EXPECT_EQ(translatePowerCapModeRedfishToDbus("Default"),
              enumPrefix + "Default");
    EXPECT_EQ(translatePowerCapModeRedfishToDbus("Enabled"),
              enumPrefix + "Enabled");
    EXPECT_EQ(translatePowerCapModeRedfishToDbus("Disabled"),
              enumPrefix + "Disabled");
    EXPECT_TRUE(translatePowerCapModeRedfishToDbus("Invalid").empty());
}

TEST(PowerCapModeCallbacks, PopulateActiveResource)
{
    auto asyncResp = std::make_shared<bmcweb::AsyncResp>();
    dbus::utility::DBusPropertiesMap properties = {
        {"CurrentMode", enumPrefix + "Enabled"}};

    afterUpdateSwitchPowerCappingModeData(
        asyncResp, "/redfish/v1/Fabrics/HGX/Switches/0", {}, properties);

    EXPECT_EQ(asyncResp->res.jsonValue["PowerCapMode"], "Enabled");
    EXPECT_EQ(asyncResp->res.jsonValue["@Redfish.Settings"]["SettingsObject"]
                                      ["@odata.id"],
              "/redfish/v1/Fabrics/HGX/Switches/0/Oem/Nvidia/"
              "PowerCappingMode/Settings");
    EXPECT_EQ(
        asyncResp->res
            .jsonValue["Actions"]["#NvidiaSwitchPowerCapMode.ResetToDefaults"]
                      ["target"],
        "/redfish/v1/Fabrics/HGX/Switches/0/Oem/Nvidia/PowerCappingMode/"
        "Actions/NvidiaSwitchPowerCapMode.ResetToDefaults");
}

TEST(PowerCapModeCallbacks, ActiveDefaultIsInternalError)
{
    auto asyncResp = std::make_shared<bmcweb::AsyncResp>();
    dbus::utility::DBusPropertiesMap properties = {
        {"CurrentMode", enumPrefix + "Default"}};

    afterUpdateSwitchPowerCappingModeData(
        asyncResp, "/redfish/v1/Fabrics/HGX/Switches/0", {}, properties);

    EXPECT_EQ(asyncResp->res.jsonValue["error"]["code"],
              "Base.1.19.InternalError");
}

TEST(PowerCapModeCallbacks, PopulateSettingsResource)
{
    auto asyncResp = std::make_shared<bmcweb::AsyncResp>();
    dbus::utility::DBusPropertiesMap properties = {
        {"PendingMode", enumPrefix + "Disabled"}};

    afterUpdateSwitchPowerCappingModeSettingsData(asyncResp, {}, properties);

    EXPECT_EQ(asyncResp->res.jsonValue["PowerCapMode"], "Disabled");
}

TEST(PowerCapModeCallbacks, SkipDefaultPendingMode)
{
    auto asyncResp = std::make_shared<bmcweb::AsyncResp>();
    dbus::utility::DBusPropertiesMap properties = {
        {"PendingMode", enumPrefix + "Default"}};

    afterUpdateSwitchPowerCappingModeSettingsData(asyncResp, {}, properties);

    EXPECT_FALSE(asyncResp->res.jsonValue.contains("PowerCapMode"));
}

TEST(PowerCapModeCallbacks, PopulateSwitchLink)
{
    auto asyncResp = std::make_shared<bmcweb::AsyncResp>();

    afterGetSwitchPowerCappingModeLink(
        asyncResp, "/redfish/v1/Fabrics/HGX/Switches/0", {},
        std::vector<std::string>{"/xyz/openbmc_project/inventory/switch0"});

    EXPECT_EQ(asyncResp->res
                  .jsonValue["Oem"]["Nvidia"]["PowerCappingMode"]["@odata.id"],
              "/redfish/v1/Fabrics/HGX/Switches/0/Oem/Nvidia/"
              "PowerCappingMode");
}

TEST(PowerCapModeCallbacks, MissingAssociationReturnsNotFound)
{
    auto asyncResp = std::make_shared<bmcweb::AsyncResp>();
    PowerCappingModeObjectHandler handler =
        [](const std::string&, const std::string&,
           const dbus::utility::MapperGetObject&) { FAIL(); };

    afterGetSwitchPowerCappingModeAssociation(asyncResp, "0", handler, {}, {});

    EXPECT_EQ(asyncResp->res.jsonValue["error"]["code"],
              "Base.1.19.ResourceNotFound");
}

TEST(PowerCapModeCallbacks, MissingDbusObjectReturnsInternalError)
{
    auto asyncResp = std::make_shared<bmcweb::AsyncResp>();
    PowerCappingModeObjectHandler handler =
        [](const std::string&, const std::string&,
           const dbus::utility::MapperGetObject&) { FAIL(); };

    afterGetSwitchPowerCappingModeDbusObject(asyncResp, "/object", handler, {},
                                             {});

    EXPECT_EQ(asyncResp->res.jsonValue["error"]["code"],
              "Base.1.19.InternalError");
}

TEST(PowerCapModeCallbacks, NonConfigurableModeIsNotWritable)
{
    auto asyncResp = std::make_shared<bmcweb::AsyncResp>();

    afterPatchSwitchPowerCappingModeGetConfigurable(
        asyncResp, enumPrefix + "Enabled", "/object", "service", {}, false);

    EXPECT_EQ(asyncResp->res.result(),
              boost::beast::http::status::method_not_allowed);
    EXPECT_EQ(asyncResp->res.jsonValue["PowerCapMode@Message.ExtendedInfo"][0]
                                      ["MessageId"],
              "Base.1.19.PropertyNotWritable");
}

TEST(PowerCapModeCallbacks, MissingAsyncObjectReturnsInternalError)
{
    auto asyncResp = std::make_shared<bmcweb::AsyncResp>();

    afterPatchSwitchPowerCappingModeGetDbusObject(
        asyncResp, enumPrefix + "Enabled", "/object", "service", {}, {});

    EXPECT_EQ(asyncResp->res.jsonValue["error"]["code"],
              "Base.1.19.InternalError");
}

// TAVMode -- mirrors the PowerCapMode block above (DGXOPENBMC-28639).

const std::string tavEnumPrefix = "com.nvidia.DeviceMode.TAVMode.TAVModeValue.";

TEST(TAVModeConversion, DbusToRedfish)
{
    EXPECT_EQ(tavModeDbusToRedfish(tavEnumPrefix + "Default"), "Default");
    EXPECT_EQ(tavModeDbusToRedfish(tavEnumPrefix + "Enabled"), "Enabled");
    EXPECT_EQ(tavModeDbusToRedfish(tavEnumPrefix + "Disabled"), "Disabled");
    EXPECT_FALSE(tavModeDbusToRedfish(tavEnumPrefix + "Invalid"));
    EXPECT_FALSE(tavModeDbusToRedfish("Enabled"));
}

TEST(TAVModeConversion, ActiveDbusToRedfish)
{
    EXPECT_FALSE(tavModeActiveDbusToRedfish(tavEnumPrefix + "Default"));
    EXPECT_EQ(tavModeActiveDbusToRedfish(tavEnumPrefix + "Enabled"), "Enabled");
    EXPECT_EQ(tavModeActiveDbusToRedfish(tavEnumPrefix + "Disabled"),
              "Disabled");
    EXPECT_FALSE(tavModeActiveDbusToRedfish(tavEnumPrefix + "Invalid"));
}

TEST(TAVModeConversion, RedfishToDbus)
{
    EXPECT_EQ(translateTAVModeRedfishToDbus("Default"),
              tavEnumPrefix + "Default");
    EXPECT_EQ(translateTAVModeRedfishToDbus("Enabled"),
              tavEnumPrefix + "Enabled");
    EXPECT_EQ(translateTAVModeRedfishToDbus("Disabled"),
              tavEnumPrefix + "Disabled");
    EXPECT_TRUE(translateTAVModeRedfishToDbus("Invalid").empty());
}

TEST(TAVModeCallbacks, PopulateActiveResource)
{
    auto asyncResp = std::make_shared<bmcweb::AsyncResp>();
    dbus::utility::DBusPropertiesMap properties = {
        {"CurrentMode", tavEnumPrefix + "Enabled"}};

    afterUpdateSwitchTAVModeData(
        asyncResp, "/redfish/v1/Fabrics/HGX/Switches/0", {}, properties);

    EXPECT_EQ(asyncResp->res.jsonValue["TAVMode"], "Enabled");
    EXPECT_EQ(asyncResp->res.jsonValue["@Redfish.Settings"]["SettingsObject"]
                                      ["@odata.id"],
              "/redfish/v1/Fabrics/HGX/Switches/0/Oem/Nvidia/"
              "TAVMode/Settings");
    EXPECT_EQ(asyncResp->res
                  .jsonValue["Actions"]["#NvidiaSwitchTAVMode.ResetToDefaults"]
                            ["target"],
              "/redfish/v1/Fabrics/HGX/Switches/0/Oem/Nvidia/TAVMode/"
              "Actions/NvidiaSwitchTAVMode.ResetToDefaults");
}

TEST(TAVModeCallbacks, ActiveDefaultIsInternalError)
{
    auto asyncResp = std::make_shared<bmcweb::AsyncResp>();
    dbus::utility::DBusPropertiesMap properties = {
        {"CurrentMode", tavEnumPrefix + "Default"}};

    afterUpdateSwitchTAVModeData(
        asyncResp, "/redfish/v1/Fabrics/HGX/Switches/0", {}, properties);

    EXPECT_EQ(asyncResp->res.jsonValue["error"]["code"],
              "Base.1.19.InternalError");
}

TEST(TAVModeCallbacks, MissingCurrentModeIsInternalError)
{
    auto asyncResp = std::make_shared<bmcweb::AsyncResp>();
    dbus::utility::DBusPropertiesMap properties = {};

    afterUpdateSwitchTAVModeData(
        asyncResp, "/redfish/v1/Fabrics/HGX/Switches/0", {}, properties);

    EXPECT_EQ(asyncResp->res.jsonValue["error"]["code"],
              "Base.1.19.InternalError");
}

TEST(TAVModeCallbacks, PopulateSettingsResource)
{
    auto asyncResp = std::make_shared<bmcweb::AsyncResp>();
    dbus::utility::DBusPropertiesMap properties = {
        {"PendingMode", tavEnumPrefix + "Disabled"}};

    afterUpdateSwitchTAVModeSettingsData(asyncResp, {}, properties);

    EXPECT_EQ(asyncResp->res.jsonValue["TAVMode"], "Disabled");
}

TEST(TAVModeCallbacks, SkipDefaultPendingMode)
{
    auto asyncResp = std::make_shared<bmcweb::AsyncResp>();
    dbus::utility::DBusPropertiesMap properties = {
        {"PendingMode", tavEnumPrefix + "Default"}};

    afterUpdateSwitchTAVModeSettingsData(asyncResp, {}, properties);

    EXPECT_FALSE(asyncResp->res.jsonValue.contains("TAVMode"));
}

TEST(TAVModeCallbacks, PopulateSwitchLink)
{
    auto asyncResp = std::make_shared<bmcweb::AsyncResp>();

    afterGetSwitchTAVModeLink(
        asyncResp, "/redfish/v1/Fabrics/HGX/Switches/0", {},
        std::vector<std::string>{"/xyz/openbmc_project/inventory/switch0"});

    EXPECT_EQ(asyncResp->res.jsonValue["Oem"]["Nvidia"]["TAVMode"]["@odata.id"],
              "/redfish/v1/Fabrics/HGX/Switches/0/Oem/Nvidia/TAVMode");
}

TEST(TAVModeCallbacks, MissingAssociationReturnsNotFound)
{
    auto asyncResp = std::make_shared<bmcweb::AsyncResp>();
    TAVModeObjectHandler handler =
        [](const std::string&, const std::string&,
           const dbus::utility::MapperGetObject&) { FAIL(); };

    afterGetSwitchTAVModeAssociation(asyncResp, "0", handler, {}, {});

    EXPECT_EQ(asyncResp->res.jsonValue["error"]["code"],
              "Base.1.19.ResourceNotFound");
}

TEST(TAVModeCallbacks, MissingDbusObjectReturnsInternalError)
{
    auto asyncResp = std::make_shared<bmcweb::AsyncResp>();
    TAVModeObjectHandler handler =
        [](const std::string&, const std::string&,
           const dbus::utility::MapperGetObject&) { FAIL(); };

    afterGetSwitchTAVModeDbusObject(asyncResp, "/object", handler, {}, {});

    EXPECT_EQ(asyncResp->res.jsonValue["error"]["code"],
              "Base.1.19.InternalError");
}

TEST(TAVModeCallbacks, NonConfigurableModeIsNotWritable)
{
    auto asyncResp = std::make_shared<bmcweb::AsyncResp>();

    afterPatchSwitchTAVModeGetConfigurable(asyncResp, tavEnumPrefix + "Enabled",
                                           "/object", "service", {}, false);

    EXPECT_EQ(asyncResp->res.result(),
              boost::beast::http::status::method_not_allowed);
    EXPECT_EQ(asyncResp->res
                  .jsonValue["TAVMode@Message.ExtendedInfo"][0]["MessageId"],
              "Base.1.19.PropertyNotWritable");
}

TEST(TAVModeCallbacks, MissingAsyncObjectReturnsInternalError)
{
    auto asyncResp = std::make_shared<bmcweb::AsyncResp>();

    afterPatchSwitchTAVModeGetDbusObject(asyncResp, tavEnumPrefix + "Enabled",
                                         "/object", "service", {}, {});

    EXPECT_EQ(asyncResp->res.jsonValue["error"]["code"],
              "Base.1.19.InternalError");
}

} // namespace
} // namespace redfish::nvidia_fabric_utils
