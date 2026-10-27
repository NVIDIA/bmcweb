// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: Copyright OpenBMC Authors
#pragma once
#include <nlohmann/json.hpp>

namespace nvidia_serial_console
{
// clang-format off

enum class SerialConsoleSource{
    Invalid,
    HostProcessor,
    SafetyController,
};

NLOHMANN_JSON_SERIALIZE_ENUM(SerialConsoleSource, {
    {SerialConsoleSource::Invalid, "Invalid"},
    {SerialConsoleSource::HostProcessor, "HostProcessor"},
    {SerialConsoleSource::SafetyController, "SafetyController"},
});

// clang-format on
} // namespace nvidia_serial_console
