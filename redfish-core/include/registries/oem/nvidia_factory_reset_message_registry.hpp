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
struct NvidiaFactoryReset
{
static constexpr Header header = {
    "Copyright 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.",
    "#MessageRegistry.v1_4_0.MessageRegistry",
    1,
    0,
    0,
    "NVIDIA Factory Reset Message Registry",
    "en",
    "This registry defines NVIDIA factory-reset messages.",
    "NvidiaFactoryReset",
    "NVIDIA",
};

static constexpr const char* url =
    "";

static constexpr std::array registry =
{
    MessageEntry{
        "AttestedWipeFailed",
        {
            "Indicates that an attested wipe failed.",
            "Attested wipe failed. Error: %1.",
            "Critical",
            1,
            {
                "string",
            },
            "Read the error in this message, correct that condition, and retry the attested wipe.",
        }},
    MessageEntry{
        "AttestedWipeSucceeded",
        {
            "Indicates that an attested wipe completed successfully.",
            "Attested wipe completed successfully.",
            "OK",
            0,
            {},
            "None.",
        }},
    MessageEntry{
        "FactoryResetIgnored",
        {
            "Indicates that a factory reset was ignored because the reset button was released early.",
            "Factory reset ignored. Button released before %1 seconds.",
            "OK",
            1,
            {
                "string",
            },
            "Hold the factory reset button for the required time to schedule a factory reset.",
        }},
    MessageEntry{
        "FactoryResetScheduled",
        {
            "Indicates that a factory reset was scheduled because the reset button was held.",
            "Factory reset scheduled. Button held for %1 seconds.",
            "Warning",
            1,
            {
                "string",
            },
            "None.",
        }},

};

enum class Index
{
    attestedWipeFailed = 0,
    attestedWipeSucceeded = 1,
    factoryResetIgnored = 2,
    factoryResetScheduled = 3,
};
}; // struct nvidia_factory_reset

[[gnu::constructor]] inline void registerNvidiaFactoryReset()
{ registerRegistry<NvidiaFactoryReset>(); }

} // namespace redfish::registries
