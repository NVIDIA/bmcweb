#pragma once
#include "registries.hpp"

#include <array>

// clang-format off
namespace redfish::registries
{
struct BiosAttributeRegistry
{
static constexpr Header header = {
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

static constexpr const char* url =
    "/redfish/v1/Registries/BiosAttributeRegistry/BiosAttributeRegistry";

static constexpr std::array<MessageEntry, 0> registry = {};
};

[[gnu::constructor]] inline void registerBiosAttributeRegistry()
{ registerRegistry<BiosAttributeRegistry>(); }

} // namespace redfish::registries
