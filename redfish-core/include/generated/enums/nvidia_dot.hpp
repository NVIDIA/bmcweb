// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: Copyright OpenBMC Authors
#pragma once
#include <nlohmann/json.hpp>

namespace nvidia_dot
{
// clang-format off

enum class FuseChangeState{
    Invalid,
    None,
    InProgress,
    Completed,
};

enum class NonceType{
    Invalid,
    DeviceUniqueIdentifier,
    RandomNonce,
    StaticValue,
};

enum class UnlockType{
    Invalid,
    OwnerUnlock,
    VendorUnlock,
};

enum class AuthenticationScheme{
    Invalid,
    ECDSA,
    Hybrid,
};

enum class DOTState{
    Invalid,
    Uninitialized,
    Volatile,
    MutableLocked,
    MutableDisabled,
};

NLOHMANN_JSON_SERIALIZE_ENUM(FuseChangeState, {
    {FuseChangeState::Invalid, "Invalid"},
    {FuseChangeState::None, "None"},
    {FuseChangeState::InProgress, "InProgress"},
    {FuseChangeState::Completed, "Completed"},
});

NLOHMANN_JSON_SERIALIZE_ENUM(NonceType, {
    {NonceType::Invalid, "Invalid"},
    {NonceType::DeviceUniqueIdentifier, "DeviceUniqueIdentifier"},
    {NonceType::RandomNonce, "RandomNonce"},
    {NonceType::StaticValue, "StaticValue"},
});

NLOHMANN_JSON_SERIALIZE_ENUM(UnlockType, {
    {UnlockType::Invalid, "Invalid"},
    {UnlockType::OwnerUnlock, "OwnerUnlock"},
    {UnlockType::VendorUnlock, "VendorUnlock"},
});

NLOHMANN_JSON_SERIALIZE_ENUM(AuthenticationScheme, {
    {AuthenticationScheme::Invalid, "Invalid"},
    {AuthenticationScheme::ECDSA, "ECDSA"},
    {AuthenticationScheme::Hybrid, "Hybrid"},
});

NLOHMANN_JSON_SERIALIZE_ENUM(DOTState, {
    {DOTState::Invalid, "Invalid"},
    {DOTState::Uninitialized, "Uninitialized"},
    {DOTState::Volatile, "Volatile"},
    {DOTState::MutableLocked, "MutableLocked"},
    {DOTState::MutableDisabled, "MutableDisabled"},
});

}
// clang-format on
