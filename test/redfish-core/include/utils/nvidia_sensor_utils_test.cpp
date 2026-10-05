// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: Copyright OpenBMC Authors
#include "utils/nvidia_sensor_utils.hpp"

#include <nlohmann/json.hpp>

#include <string>
#include <vector>

#include <gtest/gtest.h>

namespace redfish::nvidia_sensor_utils
{
namespace
{

TEST(AddFabricSwitchRelatedItems, AddsEveryFabricSwitchCombination)
{
    nlohmann::json items = nlohmann::json::array();
    const std::vector<std::string> fabricPaths = {
        "/xyz/openbmc_project/inventory/fabrics/HGX",
        "/xyz/openbmc_project/inventory/fabrics/NVLink"};
    const std::vector<std::string> switchPaths = {
        "/xyz/openbmc_project/inventory/switches/Switch0",
        "/xyz/openbmc_project/inventory/switches/Switch1"};

    addFabricSwitchRelatedItems(items, fabricPaths, switchPaths);

    EXPECT_EQ(items,
              nlohmann::json::array(
                  {{{"@odata.id",
                     "/redfish/v1/Fabrics/HGX/Switches/Switch0"}},
                   {{"@odata.id",
                     "/redfish/v1/Fabrics/HGX/Switches/Switch1"}},
                   {{"@odata.id",
                     "/redfish/v1/Fabrics/NVLink/Switches/Switch0"}},
                   {{"@odata.id",
                     "/redfish/v1/Fabrics/NVLink/Switches/Switch1"}}}));
}

TEST(AddFabricSwitchRelatedItems, EmptyInputDoesNotChangeRelatedItems)
{
    nlohmann::json items =
        nlohmann::json::array({{{"@odata.id", "/redfish/v1/existing"}}});

    addFabricSwitchRelatedItems(items, {}, {});

    EXPECT_EQ(items,
              nlohmann::json::array(
                  {{{"@odata.id", "/redfish/v1/existing"}}}));
}

} // namespace
} // namespace redfish::nvidia_sensor_utils
