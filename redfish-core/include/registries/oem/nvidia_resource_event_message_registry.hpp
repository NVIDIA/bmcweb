// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: Copyright OpenBMC Authors
#pragma once
/****************************************************************
 *                 READ THIS WARNING FIRST
 * This is an auto-generated header which contains definitions
 * for Redfish DMTF defined messages.
 * DO NOT modify this registry outside of running the
 * parse_registries.py script.  The definitions contained within
 * this file are owned by DMTF.  Any modifications to these files
 * should be first pushed to the relevant registry in the DMTF
 * github organization.
 ***************************************************************/
#include "registries.hpp"

#include <array>

// clang-format off

namespace redfish::registries
{
struct NvidiaResourceEvent
{
static constexpr Header header = {
    "Copyright 2024 Nvidia. All rights reserved.",
    "#MessageRegistry.v1_4_0.MessageRegistry",
    1,
    0,
    0,
    "NVIDIA Driver and Resource Event Registry",
    "en",
    "This registry defines the driver and resource event messages for Nvidia.",
    "NvidiaResourceEvent",
    "Nvidia",
};

static constexpr const char* url =
    "";

static constexpr std::array registry =
{
    MessageEntry{
        "BmcDriverErrorsDetected",
        {
            "Indicates a driver-level failure occurred on the management controller while communicating with a device.",
            "Operation '%1' failed on management controller while communicating to device '%2' with error: %3.",
            "Critical",
            3,
            {
                "string",
                "string",
                "string",
            },
            "If problem persists, perform management controller reboot.",
        }},
    MessageEntry{
        "DeviceDriverErrorsDetected",
        {
            "Indicates a driver-level failure occurred on a device.",
            "Operation '%1' failed while communicating to device '%2' with error: %3.",
            "Critical",
            3,
            {
                "string",
                "string",
                "string",
            },
            "If problem persists, perform power cycle of the system to recover the device.",
        }},

};

enum class Index
{
    bmcDriverErrorsDetected = 0,
    deviceDriverErrorsDetected = 1,
};
}; // struct nvidia_resource_event

[[gnu::constructor]] inline void registerNvidiaResourceEvent()
{ registerRegistry<NvidiaResourceEvent>(); }

} // namespace redfish::registries
