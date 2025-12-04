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
struct NvidiaUpdate
{
static constexpr Header header = {
    "Copyright 2024 Nvidia. All rights reserved.",
    "#MessageRegistry.v1_4_0.MessageRegistry",
    1,
    0,
    0,
    "Nvidia Message Registry",
    "en",
    "This registry defines the update messages for Nvidia.",
    "NvidiaUpdate",
    "Nvidia",
};

static constexpr const char* url =
    "";

static constexpr std::array registry =
{
    MessageEntry{
        "ActionParameterNotSupported",
        {
            "Indicates that the parameter supplied for the action is not supported on the resource.",
            "The value %1 for the parameter %2 supplied is not supported on the target resource. See ActionInfo resource URI %3",
            "Warning",
            3,
            {
                "string",
                "string",
                "string",
            },
            "Choose a value from the enumeration list provided in the ActionInfo resource URI and resubmit the request.",
        }},
    MessageEntry{
        "ComponentUpdateSkipped",
        {
            "Indicates that update of component has been skipped",
            "The update operation for the component %1 is skipped because %2.",
            "OK",
            2,
            {
                "string",
                "string",
            },
            "None.",
        }},
    MessageEntry{
        "DebugTokenAlreadyInstalled",
        {
            "Indicates that the device has a token already installed and cannot finish current request.",
            "Debug token for device '%1' has already been installed.",
            "OK",
            1,
            {
                "string",
            },
            "None.",
        }},
    MessageEntry{
        "DebugTokenEraseFailed",
        {
            "Indicates that debug token erase operation has failed for the device.",
            "The operation to erase a debug token for device '%1' has failed with error '%2'",
            "OK",
            2,
            {
                "string",
                "string",
            },
            "None.",
        }},
    MessageEntry{
        "DebugTokenInstallationSuccess",
        {
            "Signifies the successful completion of debug token installation.",
            "The operation to install a debug token for device '%1' has been successfully completed.",
            "OK",
            1,
            {
                "string",
            },
            "None.",
        }},
    MessageEntry{
        "DebugTokenRequestSuccess",
        {
            "Signifies the successful completion of the debug token request.",
            "The operation to request a debug token for device '%1' has been successfully completed.",
            "OK",
            1,
            {
                "string",
            },
            "None.",
        }},
    MessageEntry{
        "DebugTokenStatusSuccess",
        {
            "Signifies the successful completion of the debug token status request.",
            "The operation to obtain a token status for device '%1' has been successfully completed.",
            "OK",
            1,
            {
                "string",
            },
            "None.",
        }},
    MessageEntry{
        "DebugTokenUnsupported",
        {
            "Indicates that the device does not support debug token functionality.",
            "Device '%1' does not support debug token functionality.",
            "OK",
            1,
            {
                "string",
            },
            "None.",
        }},
    MessageEntry{
        "DotActionResponseError",
        {
            "Indicates that an error occured for the requested DOT command.",
            "Requested DOT action has resulted in error of type '%1'.",
            "Warning",
            1,
            {
                "string",
            },
            "None.",
        }},
    MessageEntry{
        "DotMCTPStatusError",
        {
            "Indicates that an MCTP error occured for the requested DOT command.",
            "Requested DOT action has resulted in MCTP error of type '%1'.",
            "Warning",
            1,
            {
                "string",
            },
            "None.",
        }},
    MessageEntry{
        "FirmwareNotInRecovery",
        {
            "Indicates that a firmware is not in Recovery Mode",
            "Firmware %1 is not in Recovery.",
            "OK",
            1,
            {
                "string",
            },
            "None.",
        }},
    MessageEntry{
        "HeaderValueInvalid",
        {
            "Indicates that a header value is invalid.",
            "Header value '%1' for header '%2' is invalid expected value is '%3'.",
            "Critical",
            3,
            {
                "string",
                "string",
                "string",
            },
            "Check the header value and expected value and resubmit the request again.",
        }},
    MessageEntry{
        "RecoveryStarted",
        {
            "Indicates that recovery has started on a component",
            "Firmware Recovery Started on %1.",
            "OK",
            1,
            {
                "string",
            },
            "None.",
        }},
    MessageEntry{
        "RecoverySuccessful",
        {
            "Indicates that recovery has successfully completed on a component",
            "Firmware %1 is successfully recovered.",
            "OK",
            1,
            {
                "string",
            },
            "None.",
        }},
    MessageEntry{
        "StageSuccessful",
        {
            "Indicates that image is successfully staged on the device",
            "Device %1 successfully staged with image %2.",
            "OK",
            2,
            {
                "string",
                "string",
            },
            "None.",
        }},

};

enum class Index
{
    actionParameterNotSupported = 0,
    componentUpdateSkipped = 1,
    debugTokenAlreadyInstalled = 2,
    debugTokenEraseFailed = 3,
    debugTokenInstallationSuccess = 4,
    debugTokenRequestSuccess = 5,
    debugTokenStatusSuccess = 6,
    debugTokenUnsupported = 7,
    dotActionResponseError = 8,
    dotMCTPStatusError = 9,
    firmwareNotInRecovery = 10,
    headerValueInvalid = 11,
    recoveryStarted = 12,
    recoverySuccessful = 13,
    stageSuccessful = 14,
};
}; // struct nvidia_update

[[gnu::constructor]] inline void registerNvidiaUpdate()
{ registerRegistry<NvidiaUpdate>(); }

} // namespace redfish::registries
