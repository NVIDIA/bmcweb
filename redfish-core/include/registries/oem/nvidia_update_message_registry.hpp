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
    2,
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
        "DebugTokenEraseSkipped",
        {
            "Indicates that the debug token erase operation was skipped for the device.",
            "Debug token erase operation is skipped because %1.",
            "OK",
            1,
            {
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
        "DebugTokenInstallationSkipped",
        {
            "Indicates that debug token installation has been skipped.",
            "The debug token installation is skipped because %1.",
            "OK",
            1,
            {
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
        "FirmwarePackage",
        {
            "Indicates the firmware package that the update task is applying.",
            "The firmware package is '%1'.",
            "OK",
            1,
            {
                "string",
            },
            "None.",
        }},
    MessageEntry{
        "FirmwarePackageComponentImageMissing",
        {
            "Indicates that the firmware package does not contain a required update image for a target component.",
            "The firmware update for target '%1' cannot proceed because the firmware package does not contain a required update image.",
            "Critical",
            1,
            {
                "string",
            },
            "Provide a firmware package containing update images for all applicable platform components, and retry the firmware update.",
        }},
    MessageEntry{
        "FirmwarePackageEmpty",
        {
            "Indicates that the uploaded firmware package contains no data.",
            "The uploaded firmware package '%1' is empty.",
            "Warning",
            1,
            {
                "string",
            },
            "Use a firmware package intended for this platform that contains images for the target devices, then retry the update.",
        }},
    MessageEntry{
        "FirmwarePackageSizeExceeded",
        {
            "Indicates that the uploaded firmware package is larger than the maximum size the service supports.",
            "The firmware package '%1' of size %2 exceeds the maximum supported size of %3.",
            "Critical",
            3,
            {
                "string",
                "string",
                "string",
            },
            "Reduce the firmware package size below the maximum supported size and resubmit the update.",
        }},
    MessageEntry{
        "FirmwarePackageStagingError",
        {
            "Indicates that the service could not stage the uploaded firmware package.",
            "Staging of firmware package '%1' of size %2 failed: insufficient storage or memory to complete the request.",
            "Critical",
            2,
            {
                "string",
                "string",
            },
            "Retry firmware update operation; if it persists, reboot BMC.",
        }},
    MessageEntry{
        "FirmwareUpdateInProgress",
        {
            "Indicates that a firmware update was refused because another firmware update is in progress.",
            "Firmware update task '%1' is already in progress.",
            "Warning",
            1,
            {
                "string",
            },
            "Monitor the active firmware update task and retry the update after it completes.",
        }},
    MessageEntry{
        "FirmwareUpdateInitiationError",
        {
            "Indicates that the PLDM Update Agent rejected the request to start the firmware update.",
            "The PLDM Update Agent could not access staged firmware package '%1' to start the update.",
            "Critical",
            1,
            {
                "string",
            },
            "Retry the firmware update. If the issue persists, reset the BMC and retry. If it still fails, collect the BMC logs and contact support.",
        }},
    MessageEntry{
        "FirmwareUpdateTargetInvalid",
        {
            "Indicates that a requested firmware update target does not identify an updateable resource.",
            "The firmware update target '%1' does not identify an existing, updateable firmware inventory resource.",
            "Critical",
            1,
            {
                "string",
            },
            "Verify that every URI in the Targets property identifies an existing, updateable firmware inventory resource, then resubmit the update request.",
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
        "MalformedMultipartRequest",
        {
            "Indicates that the multipart request body could not be parsed.",
            "The multipart request could not be parsed: %1.",
            "Critical",
            1,
            {
                "string",
            },
            "Correct the multipart request formatting and resubmit the request.",
        }},
    MessageEntry{
        "PreUpdateValidationFailed",
        {
            "Indicates that a firmware update request was rejected because one or more target components failed pre-update validation.",
            "The firmware update request was rejected because one or more target components failed pre-update validation.",
            "Critical",
            0,
            {},
            "Review the accompanying messages that identify the affected components, resolve the reported conditions, and retry the firmware update request.",
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
    debugTokenEraseSkipped = 7,
    debugTokenEraseSuccess = 8,
    debugTokenInstallationFailed = 9,
    debugTokenInstallationSkipped = 10,
    debugTokenInstallationSuccess = 11,
    debugTokenNotInstalled = 12,
    debugTokenRequestSuccess = 13,
    debugTokenStatusSuccess = 14,
    debugTokenUnsupported = 15,
    enterDOTRecovery = 16,
    firmwareInRecovery = 17,
    firmwareNotInRecovery = 18,
    firmwarePackage = 19,
    firmwarePackageComponentImageMissing = 20,
    firmwarePackageEmpty = 21,
    firmwarePackageSizeExceeded = 22,
    firmwarePackageStagingError = 23,
    firmwareUpdateInProgress = 24,
    firmwareUpdateInitiationError = 25,
    firmwareUpdateTargetInvalid = 26,
    headerValueInvalid = 27,
    imageCopyCompleted = 28,
    malformedMultipartRequest = 29,
    preUpdateValidationFailed = 30,
    recoveryStarted = 31,
    recoverySuccessful = 32,
};
}; // struct nvidia_update

[[gnu::constructor]] inline void registerNvidiaUpdate()
{ registerRegistry<NvidiaUpdate>(); }

} // namespace redfish::registries
