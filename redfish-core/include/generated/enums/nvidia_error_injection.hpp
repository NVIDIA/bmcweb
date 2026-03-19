// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: Copyright OpenBMC Authors
#pragma once
#include <nlohmann/json.hpp>

namespace nvidia_error_injection
{
// clang-format off

enum class ErrorType{
    Invalid,
    FatalErrors,
    PortRecoveryErrors,
    USBBridgeEmulationErrors,
    LeakDetectionErrors,
    GPIOSpoofingErrors,
};

NLOHMANN_JSON_SERIALIZE_ENUM(ErrorType, {
    {ErrorType::Invalid, "Invalid"},
    {ErrorType::FatalErrors, "FatalErrors"},
    {ErrorType::PortRecoveryErrors, "PortRecoveryErrors"},
    {ErrorType::USBBridgeEmulationErrors, "USBBridgeEmulationErrors"},
    {ErrorType::LeakDetectionErrors, "LeakDetectionErrors"},
    {ErrorType::GPIOSpoofingErrors, "GPIOSpoofingErrors"},
});

}
// clang-format on
