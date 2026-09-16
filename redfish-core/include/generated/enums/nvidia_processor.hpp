// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: Copyright OpenBMC Authors
#pragma once
#include <nlohmann/json.hpp>

namespace nvidia_processor
{
// clang-format off

enum class MLNVLPeerType{
    Invalid,
    Bridge,
    Direct,
};

enum class BMSAIMode{
    Invalid,
    Disabled,
    Production,
    DevTools,
};

enum class NVLinkEncryptionMode{
    Invalid,
    Qualification,
    Production,
};

NLOHMANN_JSON_SERIALIZE_ENUM(MLNVLPeerType, {
    {MLNVLPeerType::Invalid, "Invalid"},
    {MLNVLPeerType::Bridge, "Bridge"},
    {MLNVLPeerType::Direct, "Direct"},
});

NLOHMANN_JSON_SERIALIZE_ENUM(BMSAIMode, {
    {BMSAIMode::Invalid, "Invalid"},
    {BMSAIMode::Disabled, "Disabled"},
    {BMSAIMode::Production, "Production"},
    {BMSAIMode::DevTools, "DevTools"},
});

NLOHMANN_JSON_SERIALIZE_ENUM(NVLinkEncryptionMode, {
    {NVLinkEncryptionMode::Invalid, "Invalid"},
    {NVLinkEncryptionMode::Qualification, "Qualification"},
    {NVLinkEncryptionMode::Production, "Production"},
});

// clang-format on
} // namespace nvidia_processor
