// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: Copyright OpenBMC Authors
#pragma once
#include <nlohmann/json.hpp>

namespace nvidia_cper
{
// clang-format off

enum class SourceDeviceType{
    Invalid,
    CPU,
    GPU,
    DPU,
    NIC,
    SWX,
    BMC,
};

enum class PreSiPlatform{
    Invalid,
    Silicon,
    PreSilicon,
};

enum class EventOriginator{
    Invalid,
    Reserved1,
    PF_GSP_FW,
    VF_GSP_FW,
    PF_DRIVER,
    VF_DRIVER,
};

NLOHMANN_JSON_SERIALIZE_ENUM(SourceDeviceType, {
    {SourceDeviceType::Invalid, "Invalid"},
    {SourceDeviceType::CPU, "CPU"},
    {SourceDeviceType::GPU, "GPU"},
    {SourceDeviceType::DPU, "DPU"},
    {SourceDeviceType::NIC, "NIC"},
    {SourceDeviceType::SWX, "SWX"},
    {SourceDeviceType::BMC, "BMC"},
});

NLOHMANN_JSON_SERIALIZE_ENUM(PreSiPlatform, {
    {PreSiPlatform::Invalid, "Invalid"},
    {PreSiPlatform::Silicon, "Silicon"},
    {PreSiPlatform::PreSilicon, "PreSilicon"},
});

NLOHMANN_JSON_SERIALIZE_ENUM(EventOriginator, {
    {EventOriginator::Invalid, "Invalid"},
    {EventOriginator::Reserved1, "Reserved1"},
    {EventOriginator::PF_GSP_FW, "PF_GSP_FW"},
    {EventOriginator::VF_GSP_FW, "VF_GSP_FW"},
    {EventOriginator::PF_DRIVER, "PF_DRIVER"},
    {EventOriginator::VF_DRIVER, "VF_DRIVER"},
});

// clang-format on
} // namespace nvidia_cper
