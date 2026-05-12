/*
 * SPDX-FileCopyrightText: Copyright (c) 2021-2026 NVIDIA CORPORATION &
 * AFFILIATES. All rights reserved. SPDX-License-Identifier: Apache-2.0
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#pragma once

#include "bmcweb_config.h"

#include "dbus_utility.hpp"
#include "utils/dbus_utils.hpp"

#include <nlohmann/json.hpp>
#include <sdbusplus/unpack_properties.hpp>

#include <cstdint>
#include <optional>
#include <string>

namespace redfish
{
namespace nvidia_pcieslots
{

// Build the NVIDIA OEM json fragment for a single PCIe slot from its raw
// dbus property map. The returned object is the value to place at
// Slots[i].Oem.Nvidia. It contains the @odata.type plus any of the
// supported per-slot OEM properties whose dbus values are present and of
// the expected type. If no supported properties are present (or NVIDIA
// OEM is disabled at build time) the returned json is empty so callers
// can skip adding an Oem.Nvidia subtree at all.
//
// Supported properties (published by the NVIDIA platform daemon on
// xyz.openbmc_project.Inventory.Item.PCIeSlot):
//   - SegmentControllerIndex  (uint32)  -> integer
//   - RootPort                (uint32)  -> integer
//   - PortType                (string)  -> string
//   - PortProtocol            (string)  -> string
inline nlohmann::json buildOem(
    const dbus::utility::DBusPropertiesMap& propertiesList)
{
    nlohmann::json oem;
    if constexpr (!BMCWEB_NVIDIA_OEM_PROPERTIES)
    {
        return oem;
    }

    std::optional<uint32_t> segmentControllerIndex;
    std::optional<uint32_t> rootPort;
    std::optional<std::string> portType;
    std::optional<std::string> portProtocol;

    const bool success = sdbusplus::unpackPropertiesNoThrow(
        dbus_utils::UnpackErrorPrinter(), propertiesList,
        "SegmentControllerIndex", segmentControllerIndex, "RootPort", rootPort,
        "PortType", portType, "PortProtocol", portProtocol);
    if (!success)
    {
        return oem;
    }

    if (segmentControllerIndex)
    {
        oem["SegmentControllerIndex"] = *segmentControllerIndex;
    }
    if (rootPort)
    {
        oem["RootPort"] = *rootPort;
    }
    if (portType && !portType->empty())
    {
        oem["PortType"] = *portType;
    }
    if (portProtocol && !portProtocol->empty())
    {
        oem["PortProtocol"] = *portProtocol;
    }

    if (!oem.empty())
    {
        oem["@odata.type"] = "#NvidiaPCIeSlots.v1_1_0.NvidiaPCIeSlot";
    }
    return oem;
}

// Merge a previously-built NVIDIA OEM fragment (from buildOem) into a
// per-slot json object key-by-key, so sibling keys under Oem.Nvidia
// populated by other code paths are preserved.
inline void attachOem(nlohmann::json& slotJson, const nlohmann::json& oem)
{
    if (oem.empty())
    {
        return;
    }
    nlohmann::json& dst = slotJson["Oem"]["Nvidia"];
    for (const auto& kv : oem.items())
    {
        dst[kv.key()] = kv.value();
    }
}

} // namespace nvidia_pcieslots
} // namespace redfish
