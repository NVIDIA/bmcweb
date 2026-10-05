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
    3,
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
        "ApplyCompleteTimedOut",
        {
            "Reports a firmware update status or failure.",
            "Firmware update timed out waiting for ApplyComplete for component '%1'.",
            "Critical",
            1,
            {
                "string",
            },
            "Retry the firmware update. If the failure recurs, collect the BMC and target-device logs and contact support.",
        }},
    MessageEntry{
        "ComponentDowngradeRejected",
        {
            "Reports a firmware update status or failure.",
            "Component '%1' in the package with version '%2' has comparison stamp '%3', lower than the comparison stamp of the installed version, so the update was rejected.",
            "Critical",
            3,
            {
                "string",
                "string",
                "string",
            },
            "Use a firmware package containing an upgrade for the installed component. To intentionally downgrade firmware, set the Redfish ForceUpdate parameter to true, then retry the update.",
        }},
    MessageEntry{
        "ComponentImageAuthenticationFailed",
        {
            "Reports a firmware update status or failure.",
            "Component '%1' version '%2' in package '%3' was rejected because image authentication failed (VerifyComplete: IMAGE_AUTH_FAIL (0x95)).",
            "Critical",
            3,
            {
                "string",
                "string",
                "string",
            },
            "Obtain a correctly signed firmware package for the target device from a trusted source, then retry the firmware update. If the failure persists, collect the BMC and target-device logs and contact support.",
        }},
    MessageEntry{
        "ComponentImageIdentical",
        {
            "Indicates that a package component matches the installed component.",
            "Component '%1' in the package with version '%2' has comparison stamp '%3', matching the comparison stamp of the installed version, so the update was skipped.",
            "OK",
            3,
            {
                "string",
                "string",
                "string",
            },
            "No action is required. To reinstall the same firmware, set the Redfish ForceUpdate parameter to true.",
        }},
    MessageEntry{
        "ComponentImageSecurityVersionRejected",
        {
            "Reports a firmware update status or failure.",
            "Component '%1' version '%2' in package '%3' was rejected because its security version does not meet the device's requirement (VerifyComplete: SECURITY_VERSION_REJECTED (0x93)).",
            "Critical",
            3,
            {
                "string",
                "string",
                "string",
            },
            "Use a firmware image with a security version that meets or exceeds the device's current security version, then retry the firmware update. If the failure recurs, collect the BMC and target-device logs and contact support.",
        }},
    MessageEntry{
        "ComponentImageSigningKeyRevoked",
        {
            "Reports a firmware update status or failure.",
            "Component '%1' version '%2' in package '%3' was rejected because its signing key is revoked (VerifyComplete: SIGNING_KEY_REVOKED (0x94)).",
            "Critical",
            3,
            {
                "string",
                "string",
                "string",
            },
            "Verify the contents of the FW package. In production systems, this error suggests that a debug-signed image update was attempted without the debug token being installed. To resolve this, verify the debug token installation status using the LogService.CollectDiagnosticData API with the DebugTokenStatus parameter. If the tokens are not installed, follow the debug token installation steps and retry the update.",
        }},
    MessageEntry{
        "ComponentImageSkuMismatch",
        {
            "Reports a firmware update status or failure.",
            "Component '%1' version '%2' in package '%3' was rejected because its SKU is incompatible with the target (VerifyComplete: AP_SKU_MISMATCH (0x97)).",
            "Critical",
            3,
            {
                "string",
                "string",
                "string",
            },
            "Use a firmware package containing an image for the target SKU, then retry the firmware update. If the failure recurs, collect the BMC and target-device logs and contact support.",
        }},
    MessageEntry{
        "ComponentImageVerificationFailed",
        {
            "Reports a firmware update status or failure.",
            "Component '%1' version '%2' in package '%3' failed verification: %4.",
            "Critical",
            4,
            {
                "string",
                "string",
                "string",
                "string",
            },
            "Retry the firmware update. If the failure recurs with a known-good package, collect the BMC and target-device logs and contact support.",
        }},
    MessageEntry{
        "ComponentMetadataAuthenticationFailed",
        {
            "Reports a firmware update status or failure.",
            "Component '%1' version '%2' in package '%3' was rejected because its firmware metadata could not be authenticated (VerifyComplete: METADATA_AUTH_FAIL (0x91)).",
            "Critical",
            3,
            {
                "string",
                "string",
                "string",
            },
            "Obtain the correct firmware package for the target device from a trusted source, then retry the firmware update. If the failure persists, verify the target device's trusted-key provisioning and collect the BMC and target-device logs.",
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
        "FirmwareDownloadTimedOut",
        {
            "Reports a firmware update status or failure.",
            "Firmware download for component '%1' timed out because the device stopped requesting data after the BMC sent %2 of %3 bytes.",
            "Critical",
            3,
            {
                "string",
                "string",
                "string",
            },
            "Retry the firmware update. If the failure recurs, collect the BMC and target-device logs and contact support.",
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
        "FirmwarePackageHeaderInvalid",
        {
            "Reports a firmware update status or failure.",
            "The PLDM firmware package header is invalid or unsupported.",
            "Critical",
            0,
            {},
            "Obtain a valid firmware package for this platform from a trusted source, then retry the update. If the failure persists, collect the logs and package details, then contact support.",
        }},
    MessageEntry{
        "FirmwarePackageHeaderUnavailable",
        {
            "Reports a firmware update status or failure.",
            "PLDM firmware package header is unavailable because the package could not be read or is too short.",
            "Critical",
            0,
            {},
            "Download a fresh firmware package from a trusted source, verify it was transferred without corruption, and retry the update.",
        }},
    MessageEntry{
        "FirmwarePackageMetadataInvalid",
        {
            "Reports a firmware update status or failure.",
            "The PLDM firmware package contains invalid or unsupported package metadata.",
            "Critical",
            0,
            {},
            "Obtain a valid firmware package for this platform from a trusted source, then retry the update. If the failure persists, collect the logs and package details, then contact support.",
        }},
    MessageEntry{
        "FirmwarePackageNoMatchingDevices",
        {
            "Reports a firmware update status or failure.",
            "No discovered firmware devices match the PLDM firmware package with version '%1'.",
            "Critical",
            1,
            {
                "string",
            },
            "Verify that the firmware package is intended for this platform and contains images for the devices in this system, then retry the update.",
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
        "FirmwareTransferCompletionTimedOut",
        {
            "Reports a firmware update status or failure.",
            "The device did not send TransferComplete for component '%1' before the firmware update operation timed out.",
            "Critical",
            1,
            {
                "string",
            },
            "Retry the firmware update. If the failure recurs, collect the BMC and target-device logs and contact support.",
        }},
    MessageEntry{
        "FirmwareUpdateBlockedByWriteProtection",
        {
            "Reports a firmware update status or failure.",
            "Component '%1' could not be written to its storage because write protection is enabled.",
            "Critical",
            1,
            {
                "string",
            },
            "Disable write protection on the device, then retry the firmware update.",
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
        "FirmwareVerificationCompletionTimedOut",
        {
            "Reports a firmware update status or failure.",
            "The device did not send VerifyComplete for component '%1' before the firmware update operation timed out.",
            "Critical",
            1,
            {
                "string",
            },
            "Retry the firmware update. If the failure recurs, collect the BMC and target-device logs and contact support.",
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
        "PLDMCommandTimedOut",
        {
            "Reports a firmware update status or failure.",
            "PLDM command '%1' timed out for update target '%2'.",
            "Critical",
            2,
            {
                "string",
                "string",
            },
            "Retry the firmware update. If the failure recurs, collect the BMC and target-device logs and contact support.",
        }},
    MessageEntry{
        "PackageHeaderChecksumMismatch",
        {
            "Reports a firmware update status or failure.",
            "PLDM firmware package failed header checksum validation. Expected checksum '%1'; calculated checksum '%2'.",
            "Critical",
            2,
            {
                "string",
                "string",
            },
            "Download a fresh firmware package from a trusted source, verify it was transferred without corruption, and retry the update.",
        }},
    MessageEntry{
        "PackagePayloadChecksumMismatch",
        {
            "Reports a firmware update status or failure.",
            "PLDM firmware package with version '%1' failed payload checksum validation. Expected checksum '%2'; calculated checksum '%3'.",
            "Critical",
            3,
            {
                "string",
                "string",
                "string",
            },
            "Download a fresh firmware package from a trusted source, verify it was transferred without corruption, and retry the update.",
        }},
    MessageEntry{
        "PackageSignatureIntegrityFailed",
        {
            "Reports a firmware update status or failure.",
            "The integrity check using the NVIDIA signature header failed for PLDM firmware package version '%1'.",
            "Critical",
            1,
            {
                "string",
            },
            "Download a fresh firmware package from a trusted source, verify it was transferred without corruption, and retry the update.",
        }},
    MessageEntry{
        "PackageSignatureRequired",
        {
            "Reports a firmware update status or failure.",
            "PLDM firmware package with version '%1' was rejected because the BMC requires an NVIDIA signature header to authenticate firmware packages.",
            "Critical",
            1,
            {
                "string",
            },
            "Obtain a firmware package signed by an authority trusted by this BMC and containing the required NVIDIA signature header, then retry the update.",
        }},
    MessageEntry{
        "PackageSignatureVerificationFailed",
        {
            "Reports a firmware update status or failure.",
            "Signature verification using the BMC-configured key failed for PLDM firmware package version '%1'.",
            "Critical",
            1,
            {
                "string",
            },
            "Obtain a firmware package signed by an authority trusted by this BMC, then retry the update. If signature verification still fails with a known-good package, verify that the BMC is provisioned with the required verification key. If the issue persists, collect the logs and contact support.",
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
    MessageEntry{
        "RequestedTargetNotSupportedByPackage",
        {
            "Reports a firmware update status or failure.",
            "Requested firmware update target '%1' is not supported by PLDM firmware package version '%2'.",
            "Critical",
            2,
            {
                "string",
                "string",
            },
            "Verify that the firmware package is intended for this platform and contains an image compatible with the requested target, then retry the firmware update.",
        }},
    MessageEntry{
        "RequestedTargetUnavailableForUpdate",
        {
            "Reports a firmware update status or failure.",
            "Requested firmware update target '%1' is unavailable because it was not discovered during PLDM firmware inventory refresh.",
            "Critical",
            1,
            {
                "string",
            },
            "Power on the target and verify its management connection, then retry the firmware update. If the target remains unavailable, follow the platform firmware recovery procedure.",
        }},
    MessageEntry{
        "SignatureHeaderMalformed",
        {
            "Reports a firmware update status or failure.",
            "The NVIDIA signature header for PLDM firmware package version '%1' failed validation: %2.",
            "Critical",
            2,
            {
                "string",
                "string",
            },
            "Obtain a valid firmware package for this platform from a trusted source, then retry the update. If the failure persists, collect the logs and package details, then contact support.",
        }},
    MessageEntry{
        "SignatureHeaderVersionUnsupported",
        {
            "Reports a firmware update status or failure.",
            "PLDM firmware package version '%1' uses unsupported NVIDIA signature-header major version '%2'.",
            "Critical",
            2,
            {
                "string",
                "string",
            },
            "Obtain a firmware package that uses an NVIDIA signature-header version supported by this platform, then retry the update.",
        }},

};

enum class Index
{
    activateSuccessful = 0,
    applyCompleteTimedOut = 1,
    componentDowngradeRejected = 2,
    componentImageAuthenticationFailed = 3,
    componentImageIdentical = 4,
    componentImageSecurityVersionRejected = 5,
    componentImageSigningKeyRevoked = 6,
    componentImageSkuMismatch = 7,
    componentImageVerificationFailed = 8,
    componentMetadataAuthenticationFailed = 9,
    componentUpdateSkipped = 10,
    componentUpdateTime = 11,
    dOTActionResponseError = 12,
    dOTMCTPStatusError = 13,
    debugTokenAlreadyInstalled = 14,
    debugTokenEraseFailed = 15,
    debugTokenEraseSkipped = 16,
    debugTokenEraseSuccess = 17,
    debugTokenInstallationFailed = 18,
    debugTokenInstallationSkipped = 19,
    debugTokenInstallationSuccess = 20,
    debugTokenNotInstalled = 21,
    debugTokenRequestSuccess = 22,
    debugTokenStatusSuccess = 23,
    debugTokenUnsupported = 24,
    enterDOTRecovery = 25,
    firmwareDownloadTimedOut = 26,
    firmwareInRecovery = 27,
    firmwareNotInRecovery = 28,
    firmwarePackage = 29,
    firmwarePackageComponentImageMissing = 30,
    firmwarePackageEmpty = 31,
    firmwarePackageHeaderInvalid = 32,
    firmwarePackageHeaderUnavailable = 33,
    firmwarePackageMetadataInvalid = 34,
    firmwarePackageNoMatchingDevices = 35,
    firmwarePackageSizeExceeded = 36,
    firmwarePackageStagingError = 37,
    firmwareTransferCompletionTimedOut = 38,
    firmwareUpdateBlockedByWriteProtection = 39,
    firmwareUpdateInProgress = 40,
    firmwareUpdateInitiationError = 41,
    firmwareUpdateTargetInvalid = 42,
    firmwareVerificationCompletionTimedOut = 43,
    headerValueInvalid = 44,
    imageCopyCompleted = 45,
    malformedMultipartRequest = 46,
    pLDMCommandTimedOut = 47,
    packageHeaderChecksumMismatch = 48,
    packagePayloadChecksumMismatch = 49,
    packageSignatureIntegrityFailed = 50,
    packageSignatureRequired = 51,
    packageSignatureVerificationFailed = 52,
    preUpdateValidationFailed = 53,
    recoveryStarted = 54,
    recoverySuccessful = 55,
    requestedTargetNotSupportedByPackage = 56,
    requestedTargetUnavailableForUpdate = 57,
    signatureHeaderMalformed = 58,
    signatureHeaderVersionUnsupported = 59,
};
}; // struct nvidia_update

[[gnu::constructor]] inline void registerNvidiaUpdate()
{ registerRegistry<NvidiaUpdate>(); }

} // namespace redfish::registries
