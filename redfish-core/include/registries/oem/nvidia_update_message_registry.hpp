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
    1,
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
        "ActivateSuccessful",
        {
            "Indicates that image is successfully activated on the device",
            "Device %1 is successfully activated with image %2.",
            "OK",
            2,
            {
                "string",
                "string",
            },
            "None.",
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
        "ComponentUpdateTime",
        {
            "Indicates the time taken to update a component",
            "The update operation for component '%1' completed in '%2'.",
            "OK",
            2,
            {
                "string",
                "string",
            },
            "None.",
        }},
    MessageEntry{
        "DOTActionResponseError",
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
        "DOTMCTPStatusError",
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
        "DebugTokenEraseSuccess",
        {
            "Signifies the successful completion of debug token erase.",
            "The operation to erase a debug token for device '%1' has been successfully completed.",
            "OK",
            1,
            {
                "string",
            },
            "None.",
        }},
    MessageEntry{
        "DebugTokenInstallationFailed",
        {
            "Indicates that debug token installation operation has failed for the device.",
            "The operation to install a debug token for device '%1' has failed with error '%2'",
            "Critical",
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
        "DebugTokenNotInstalled",
        {
            "Indicates that no debug token was installed on the device.",
            "Debug token is not installed on device '%1'.",
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
        "EnterDOTRecovery",
        {
            "Indicates that the device has accepted an empty DOT blob and has successfully entered DOT recovery mode.",
            "The device %1 has accepted an empty DOT Blob and has successfully entered DOT recovery mode.",
            "OK",
            1,
            {
                "string",
            },
            "Perform an L1 reset, then proceed with a second firmware recovery update to initiate DOT recovery NSM commands, such as DOTOverride.",
        }},
    MessageEntry{
        "FirmwareInRecovery",
        {
            "Indicates that device had boot failure and currently entered firmware recovery mode which requires external fw recovery",
            "Firmware %1 is in Recovery.",
            "Critical",
            1,
            {
                "string",
            },
            "Perform device FW recovery",
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
        "ImageCopyCompleted",
        {
            "Indicates that image copy had already been completed successfully.",
            "Image copy had already been completed successfully for '%1'.",
            "OK",
            1,
            {
                "string",
            },
            "None.",
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

};

enum class Index
{
    activateSuccessful = 0,
    componentUpdateSkipped = 1,
    componentUpdateTime = 2,
    dOTActionResponseError = 3,
    dOTMCTPStatusError = 4,
    debugTokenAlreadyInstalled = 5,
    debugTokenEraseFailed = 6,
    debugTokenEraseSuccess = 7,
    debugTokenInstallationFailed = 8,
    debugTokenInstallationSuccess = 9,
    debugTokenNotInstalled = 10,
    debugTokenRequestSuccess = 11,
    debugTokenStatusSuccess = 12,
    debugTokenUnsupported = 13,
    enterDOTRecovery = 14,
    firmwareInRecovery = 15,
    firmwareNotInRecovery = 16,
    headerValueInvalid = 17,
    imageCopyCompleted = 18,
    recoveryStarted = 19,
    recoverySuccessful = 20,
};
}; // struct nvidia_update

[[gnu::constructor]] inline void registerNvidiaUpdate()
{ registerRegistry<NvidiaUpdate>(); }

} // namespace redfish::registries
