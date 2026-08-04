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
struct NvidiaNvlink
{
static constexpr Header header = {
    "Copyright 2024 Nvidia. All rights reserved.",
    "#MessageRegistry.v1_4_0.MessageRegistry",
    1,
    0,
    0,
    "NVIDIA Message Registry",
    "en",
    "This registry defines messages for NVIDIA platform events.",
    "NvidiaMessageRegistry",
    "Nvidia",
};

static constexpr const char* url =
    "";

static constexpr std::array registry =
{
    MessageEntry{
        "NVLinkPortHealthStateChanged",
        {
            "Indicates that the NVLink early health state of a port has changed.",
            "NVLink port '%1' health state changed to '%2' (trigger: '%3').",
            "Warning",
            3,
            {
                "string",
                "string",
                "string",
            },
            "If health state is Attention, review the AttentionTriggerReason and monitor port performance. Contact NVIDIA support if the condition persists.",
        }},

};

enum class Index
{
    nVLinkPortHealthStateChanged = 0,
};
}; // struct nvidia_nvlink

[[gnu::constructor]] inline void registerNvidiaNvlink()
{ registerRegistry<NvidiaNvlink>(); }

} // namespace redfish::registries
