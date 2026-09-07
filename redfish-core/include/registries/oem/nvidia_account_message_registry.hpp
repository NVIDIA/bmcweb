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
struct NvidiaAccount
{
static constexpr Header header = {
    "Copyright 2026 Nvidia. All rights reserved.",
    "#MessageRegistry.v1_4_0.MessageRegistry",
    1,
    0,
    0,
    "NVIDIA Account Message Registry",
    "en",
    "This registry defines the account management messages for Nvidia.",
    "NvidiaAccount",
    "Nvidia",
};

static constexpr const char* url =
    "";

static constexpr std::array registry =
{
    MessageEntry{
        "AccountTypeRestricted",
        {
            "Indicates that the requested value/s for the property are not allowed for this account.",
            "The requested value/s for the property %1 are not allowed for this account.",
            "Warning",
            1,
            {
                "string",
            },
            "Remove the value/s that are not allowed and resubmit the request.",
        }},

};

enum class Index
{
    accountTypeRestricted = 0,
};
}; // struct nvidia_account

[[gnu::constructor]] inline void registerNvidiaAccount()
{ registerRegistry<NvidiaAccount>(); }

} // namespace redfish::registries
