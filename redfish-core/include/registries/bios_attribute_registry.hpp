#pragma once
#include "registries.hpp"

#include <array>

namespace redfish::registries::bios
{
const Header header = {
    "Copyright 2022 OpenBMC. All rights reserved.",
    "#MessageRegistry.v1_4_0.MessageRegistry",
    1,
    0,
    0,
    "Bios Attribute Registry",
    "en",
    "This registry defines the base messages for Bios Attribute Registry.",
    "BiosAttributeRegistry",
    "OpenBMC",
};

constexpr const char* url =
    "/redfish/v1/Registries/BiosAttributeRegistry/BiosAttributeRegistry";

constexpr std::array<MessageEntry, 0> registry = {};
} // namespace redfish::registries::bios
