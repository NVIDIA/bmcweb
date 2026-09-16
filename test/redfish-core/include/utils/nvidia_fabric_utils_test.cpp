// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES.
// All rights reserved. SPDX-License-Identifier: Apache-2.0

#include "async_resp.hpp"
#include "dbus_utility.hpp"
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

constexpr const char* dbusDefault =
    "com.nvidia.DeviceMode.PowerCappingMode.PowerCapMode.Default";
constexpr const char* dbusEnabled =
    "com.nvidia.DeviceMode.PowerCappingMode.PowerCapMode.Enabled";
constexpr const char* dbusDisabled =
    "com.nvidia.DeviceMode.PowerCappingMode.PowerCapMode.Disabled";

TEST(PowerCapModeConversion, DbusToRedfish)
{
    EXPECT_EQ(translatePowerCapModeDbusToRedfish(dbusDefault), "Default");
    EXPECT_EQ(translatePowerCapModeDbusToRedfish(dbusEnabled), "Enabled");
    EXPECT_EQ(translatePowerCapModeDbusToRedfish(dbusDisabled), "Disabled");
    EXPECT_TRUE(translatePowerCapModeDbusToRedfish(
                    "com.nvidia.DeviceMode.PowerCappingMode.PowerCapMode."
                    "Invalid")
                    .empty());
    EXPECT_TRUE(translatePowerCapModeDbusToRedfish("Enabled").empty());
}

TEST(PowerCapModeConversion, RedfishToDbus)
{
    EXPECT_EQ(translatePowerCapModeRedfishToDbus("Default"), dbusDefault);
    EXPECT_EQ(translatePowerCapModeRedfishToDbus("Enabled"), dbusEnabled);
    EXPECT_EQ(translatePowerCapModeRedfishToDbus("Disabled"), dbusDisabled);
    EXPECT_TRUE(translatePowerCapModeRedfishToDbus("Invalid").empty());
}

TEST(PowerCapModeCallbacks, PopulateActiveResource)
{
    auto asyncResp = std::make_shared<bmcweb::AsyncResp>();
    dbus::utility::DBusPropertiesMap properties = {
        {"CurrentMode", std::string(dbusEnabled)}};

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
        {"CurrentMode", std::string(dbusDefault)}};

    afterUpdateSwitchPowerCappingModeData(
        asyncResp, "/redfish/v1/Fabrics/HGX/Switches/0", {}, properties);

    EXPECT_EQ(asyncResp->res.jsonValue["error"]["code"],
              "Base.1.19.InternalError");
}

TEST(PowerCapModeCallbacks, PopulateSettingsResource)
{
    auto asyncResp = std::make_shared<bmcweb::AsyncResp>();
    dbus::utility::DBusPropertiesMap properties = {
        {"PendingMode", std::string(dbusDisabled)}};

    afterUpdateSwitchPowerCappingModeSettingsData(asyncResp, {}, properties);

    EXPECT_EQ(asyncResp->res.jsonValue["PowerCapMode"], "Disabled");
}

TEST(PowerCapModeCallbacks, SkipDefaultPendingMode)
{
    auto asyncResp = std::make_shared<bmcweb::AsyncResp>();
    dbus::utility::DBusPropertiesMap properties = {
        {"PendingMode", std::string(dbusDefault)}};

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
        asyncResp, dbusEnabled, "/object", "service", {}, false);

    EXPECT_EQ(asyncResp->res.result(),
              boost::beast::http::status::method_not_allowed);
    EXPECT_EQ(asyncResp->res.jsonValue["PowerCapMode@Message.ExtendedInfo"][0]
                                      ["MessageId"],
              "Base.1.19.PropertyNotWritable");
}

TEST(PowerCapModeCallbacks, MissingAsyncObjectReturnsInternalError)
{
    auto asyncResp = std::make_shared<bmcweb::AsyncResp>();

    afterPatchSwitchPowerCappingModeGetDbusObject(asyncResp, dbusEnabled,
                                                  "/object", "service", {}, {});

    EXPECT_EQ(asyncResp->res.jsonValue["error"]["code"],
              "Base.1.19.InternalError");
}

// TAVMode -- mirrors the PowerCapMode block above (DGXOPENBMC-28639).

constexpr const char* tavDbusDefault =
    "com.nvidia.DeviceMode.TAVMode.TAVModeValue.Default";
constexpr const char* tavDbusEnabled =
    "com.nvidia.DeviceMode.TAVMode.TAVModeValue.Enabled";
constexpr const char* tavDbusDisabled =
    "com.nvidia.DeviceMode.TAVMode.TAVModeValue.Disabled";

TEST(TAVModeConversion, DbusToRedfish)
{
    EXPECT_EQ(tavModeDbusToRedfish(tavDbusDefault), "Default");
    EXPECT_EQ(tavModeDbusToRedfish(tavDbusEnabled), "Enabled");
    EXPECT_EQ(tavModeDbusToRedfish(tavDbusDisabled), "Disabled");
    EXPECT_FALSE(tavModeDbusToRedfish(
        "com.nvidia.DeviceMode.TAVMode.TAVModeValue.Invalid"));
    EXPECT_FALSE(tavModeDbusToRedfish("Enabled"));
}

TEST(TAVModeConversion, ActiveDbusToRedfish)
{
    EXPECT_FALSE(tavModeActiveDbusToRedfish(tavDbusDefault));
    EXPECT_EQ(tavModeActiveDbusToRedfish(tavDbusEnabled), "Enabled");
    EXPECT_EQ(tavModeActiveDbusToRedfish(tavDbusDisabled), "Disabled");
    EXPECT_FALSE(tavModeActiveDbusToRedfish(
        "com.nvidia.DeviceMode.TAVMode.TAVModeValue.Invalid"));
}

TEST(TAVModeConversion, RedfishToDbus)
{
    EXPECT_EQ(translateTAVModeRedfishToDbus("Default"), tavDbusDefault);
    EXPECT_EQ(translateTAVModeRedfishToDbus("Enabled"), tavDbusEnabled);
    EXPECT_EQ(translateTAVModeRedfishToDbus("Disabled"), tavDbusDisabled);
    EXPECT_TRUE(translateTAVModeRedfishToDbus("Invalid").empty());
}

TEST(TAVModeCallbacks, PopulateActiveResource)
{
    auto asyncResp = std::make_shared<bmcweb::AsyncResp>();
    dbus::utility::DBusPropertiesMap properties = {
        {"CurrentMode", std::string(tavDbusEnabled)}};

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
        {"CurrentMode", std::string(tavDbusDefault)}};

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
        {"PendingMode", std::string(tavDbusDisabled)}};

    afterUpdateSwitchTAVModeSettingsData(asyncResp, {}, properties);

    EXPECT_EQ(asyncResp->res.jsonValue["TAVMode"], "Disabled");
}

TEST(TAVModeCallbacks, SkipDefaultPendingMode)
{
    auto asyncResp = std::make_shared<bmcweb::AsyncResp>();
    dbus::utility::DBusPropertiesMap properties = {
        {"PendingMode", std::string(tavDbusDefault)}};

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

    afterPatchSwitchTAVModeGetConfigurable(asyncResp, tavDbusEnabled, "/object",
                                           "service", {}, false);

    EXPECT_EQ(asyncResp->res.result(),
              boost::beast::http::status::method_not_allowed);
    EXPECT_EQ(asyncResp->res
                  .jsonValue["TAVMode@Message.ExtendedInfo"][0]["MessageId"],
              "Base.1.19.PropertyNotWritable");
}

TEST(TAVModeCallbacks, MissingAsyncObjectReturnsInternalError)
{
    auto asyncResp = std::make_shared<bmcweb::AsyncResp>();

    afterPatchSwitchTAVModeGetDbusObject(asyncResp, tavDbusEnabled, "/object",
                                         "service", {}, {});

    EXPECT_EQ(asyncResp->res.jsonValue["error"]["code"],
              "Base.1.19.InternalError");
}

} // namespace
} // namespace redfish::nvidia_fabric_utils
