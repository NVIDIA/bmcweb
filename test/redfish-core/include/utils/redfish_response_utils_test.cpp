// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: Copyright (c) 2025 NVIDIA CORPORATION & AFFILIATES.
#include "utils/redfish_response_utils.hpp"

#include <nlohmann/json.hpp>

#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>

#include <gtest/gtest.h>

namespace redfish
{
namespace
{

// =====================================================================
// String — mapValidOrNull
// =====================================================================

TEST(StringOrNull, NullptrOmitsKey)
{
    nlohmann::json j;
    mapValidOrNull(j, "Key", static_cast<const std::string*>(nullptr));
    EXPECT_FALSE(j.contains("Key"));
}

TEST(StringOrNull, EmptySetsNull)
{
    nlohmann::json j;
    std::string val;
    mapValidOrNull(j, "Key", &val);
    EXPECT_TRUE(j.contains("Key"));
    EXPECT_TRUE(j["Key"].is_null());
}

TEST(StringOrNull, NotSupportedOmitsKey)
{
    nlohmann::json j;
    std::string val = "NOT_SUPPORTED";
    mapValidOrNull(j, "Key", &val);
    EXPECT_FALSE(j.contains("Key"));
}

TEST(StringOrNull, ValidValueSetsValue)
{
    nlohmann::json j;
    std::string val = "ABC123";
    mapValidOrNull(j, "Key", &val);
    EXPECT_EQ(j["Key"], "ABC123");
}

// =====================================================================
// String — mapValidOrOmit
// =====================================================================

TEST(StringOrOmit, NullptrOmitsKey)
{
    nlohmann::json j;
    mapValidOrOmit(j, "Key", static_cast<const std::string*>(nullptr));
    EXPECT_FALSE(j.contains("Key"));
}

TEST(StringOrOmit, EmptyOmitsKey)
{
    nlohmann::json j;
    std::string val;
    mapValidOrOmit(j, "Key", &val);
    EXPECT_FALSE(j.contains("Key"));
}

TEST(StringOrOmit, NotSupportedOmitsKey)
{
    nlohmann::json j;
    std::string val = "NOT_SUPPORTED";
    mapValidOrOmit(j, "Key", &val);
    EXPECT_FALSE(j.contains("Key"));
}

TEST(StringOrOmit, ValidValueSetsValue)
{
    nlohmann::json j;
    std::string val = "ABC123";
    mapValidOrOmit(j, "Key", &val);
    EXPECT_EQ(j["Key"], "ABC123");
}

// =====================================================================
// String — mapValidOrEmpty
// =====================================================================

TEST(StringOrEmpty, NullptrOmitsKey)
{
    nlohmann::json j;
    mapValidOrEmpty(j, "Key", static_cast<const std::string*>(nullptr));
    EXPECT_FALSE(j.contains("Key"));
}

TEST(StringOrEmpty, EmptySetsEmptyString)
{
    nlohmann::json j;
    std::string val;
    mapValidOrEmpty(j, "Key", &val);
    EXPECT_EQ(j["Key"], "");
}

TEST(StringOrEmpty, NotSupportedOmitsKey)
{
    nlohmann::json j;
    std::string val = "NOT_SUPPORTED";
    mapValidOrEmpty(j, "Key", &val);
    EXPECT_FALSE(j.contains("Key"));
}

TEST(StringOrEmpty, ValidValueSetsValue)
{
    nlohmann::json j;
    std::string val = "ABC123";
    mapValidOrEmpty(j, "Key", &val);
    EXPECT_EQ(j["Key"], "ABC123");
}

// =====================================================================
// uint32_t — mapValidOrNull / mapValidOrOmit
// =====================================================================

TEST(Uint32OrNull, NullptrOmitsKey)
{
    nlohmann::json j;
    mapValidOrNull(j, "Key", static_cast<const uint32_t*>(nullptr));
    EXPECT_FALSE(j.contains("Key"));
}

TEST(Uint32OrNull, TombstoneSetsNull)
{
    nlohmann::json j;
    uint32_t val = std::numeric_limits<uint32_t>::max();
    mapValidOrNull(j, "Key", &val);
    EXPECT_TRUE(j.contains("Key"));
    EXPECT_TRUE(j["Key"].is_null());
}

TEST(Uint32OrNull, ValidValueSetsValue)
{
    nlohmann::json j;
    uint32_t val = 42;
    mapValidOrNull(j, "Key", &val);
    EXPECT_EQ(j["Key"], 42);
}

TEST(Uint32OrOmit, NullptrOmitsKey)
{
    nlohmann::json j;
    mapValidOrOmit(j, "Key", static_cast<const uint32_t*>(nullptr));
    EXPECT_FALSE(j.contains("Key"));
}

TEST(Uint32OrOmit, TombstoneOmitsKey)
{
    nlohmann::json j;
    uint32_t val = std::numeric_limits<uint32_t>::max();
    mapValidOrOmit(j, "Key", &val);
    EXPECT_FALSE(j.contains("Key"));
}

TEST(Uint32OrOmit, ValidValueSetsValue)
{
    nlohmann::json j;
    uint32_t val = 42;
    mapValidOrOmit(j, "Key", &val);
    EXPECT_EQ(j["Key"], 42);
}

// =====================================================================
// uint64_t — mapValidOrNull / mapValidOrOmit
// =====================================================================

TEST(Uint64OrNull, NullptrOmitsKey)
{
    nlohmann::json j;
    mapValidOrNull(j, "Key", static_cast<const uint64_t*>(nullptr));
    EXPECT_FALSE(j.contains("Key"));
}

TEST(Uint64OrNull, TombstoneSetsNull)
{
    nlohmann::json j;
    uint64_t val = std::numeric_limits<uint64_t>::max();
    mapValidOrNull(j, "Key", &val);
    EXPECT_TRUE(j.contains("Key"));
    EXPECT_TRUE(j["Key"].is_null());
}

TEST(Uint64OrNull, ValidValueSetsValue)
{
    nlohmann::json j;
    uint64_t val = 100000;
    mapValidOrNull(j, "Key", &val);
    EXPECT_EQ(j["Key"], 100000);
}

TEST(Uint64OrOmit, NullptrOmitsKey)
{
    nlohmann::json j;
    mapValidOrOmit(j, "Key", static_cast<const uint64_t*>(nullptr));
    EXPECT_FALSE(j.contains("Key"));
}

TEST(Uint64OrOmit, TombstoneOmitsKey)
{
    nlohmann::json j;
    uint64_t val = std::numeric_limits<uint64_t>::max();
    mapValidOrOmit(j, "Key", &val);
    EXPECT_FALSE(j.contains("Key"));
}

TEST(Uint64OrOmit, ValidValueSetsValue)
{
    nlohmann::json j;
    uint64_t val = 100000;
    mapValidOrOmit(j, "Key", &val);
    EXPECT_EQ(j["Key"], 100000);
}

// =====================================================================
// size_t — mapValidOrNull / mapValidOrOmit
// =====================================================================

TEST(SizetOrNull, NullptrOmitsKey)
{
    nlohmann::json j;
    mapValidOrNull(j, "Key", static_cast<const size_t*>(nullptr));
    EXPECT_FALSE(j.contains("Key"));
}

TEST(SizetOrNull, TombstoneSetsNull)
{
    nlohmann::json j;
    size_t val = std::numeric_limits<size_t>::max();
    mapValidOrNull(j, "Key", &val);
    EXPECT_TRUE(j.contains("Key"));
    EXPECT_TRUE(j["Key"].is_null());
}

TEST(SizetOrNull, ValidValueSetsValue)
{
    nlohmann::json j;
    size_t val = 256;
    mapValidOrNull(j, "Key", &val);
    EXPECT_EQ(j["Key"], 256);
}

TEST(SizetOrOmit, NullptrOmitsKey)
{
    nlohmann::json j;
    mapValidOrOmit(j, "Key", static_cast<const size_t*>(nullptr));
    EXPECT_FALSE(j.contains("Key"));
}

TEST(SizetOrOmit, TombstoneOmitsKey)
{
    nlohmann::json j;
    size_t val = std::numeric_limits<size_t>::max();
    mapValidOrOmit(j, "Key", &val);
    EXPECT_FALSE(j.contains("Key"));
}

TEST(SizetOrOmit, ValidValueSetsValue)
{
    nlohmann::json j;
    size_t val = 256;
    mapValidOrOmit(j, "Key", &val);
    EXPECT_EQ(j["Key"], 256);
}

// =====================================================================
// double — mapValidOrNull / mapValidOrOmit
// =====================================================================

TEST(DoubleOrNull, NullptrOmitsKey)
{
    nlohmann::json j;
    mapValidOrNull(j, "Key", static_cast<const double*>(nullptr));
    EXPECT_FALSE(j.contains("Key"));
}

TEST(DoubleOrNull, NanSetsNull)
{
    nlohmann::json j;
    double val = std::numeric_limits<double>::quiet_NaN();
    mapValidOrNull(j, "Key", &val);
    EXPECT_TRUE(j.contains("Key"));
    EXPECT_TRUE(j["Key"].is_null());
}

TEST(DoubleOrNull, ValidValueSetsValue)
{
    nlohmann::json j;
    double val = 25.0;
    mapValidOrNull(j, "Key", &val);
    EXPECT_DOUBLE_EQ(j["Key"].get<double>(), 25.0);
}

TEST(DoubleOrOmit, NullptrOmitsKey)
{
    nlohmann::json j;
    mapValidOrOmit(j, "Key", static_cast<const double*>(nullptr));
    EXPECT_FALSE(j.contains("Key"));
}

TEST(DoubleOrOmit, NanOmitsKey)
{
    nlohmann::json j;
    double val = std::numeric_limits<double>::quiet_NaN();
    mapValidOrOmit(j, "Key", &val);
    EXPECT_FALSE(j.contains("Key"));
}

TEST(DoubleOrOmit, ValidValueSetsValue)
{
    nlohmann::json j;
    double val = 25.0;
    mapValidOrOmit(j, "Key", &val);
    EXPECT_DOUBLE_EQ(j["Key"].get<double>(), 25.0);
}

// =====================================================================
// int64_t (signed) — exercises the generic integer path
// =====================================================================

TEST(Int64OrNull, TombstoneSetsNull)
{
    nlohmann::json j;
    int64_t val = std::numeric_limits<int64_t>::max();
    mapValidOrNull(j, "Key", &val);
    EXPECT_TRUE(j.contains("Key"));
    EXPECT_TRUE(j["Key"].is_null());
}

TEST(Int64OrNull, ValidValueSetsValue)
{
    nlohmann::json j;
    int64_t val = -42;
    mapValidOrNull(j, "Key", &val);
    EXPECT_EQ(j["Key"], -42);
}

TEST(Int64OrOmit, TombstoneOmitsKey)
{
    nlohmann::json j;
    int64_t val = std::numeric_limits<int64_t>::max();
    mapValidOrOmit(j, "Key", &val);
    EXPECT_FALSE(j.contains("Key"));
}

// =====================================================================
// uint16_t (narrow) — exercises the generic integer path
// =====================================================================

TEST(Uint16OrNull, TombstoneSetsNull)
{
    nlohmann::json j;
    uint16_t val = std::numeric_limits<uint16_t>::max();
    mapValidOrNull(j, "Key", &val);
    EXPECT_TRUE(j.contains("Key"));
    EXPECT_TRUE(j["Key"].is_null());
}

TEST(Uint16OrNull, ValidValueSetsValue)
{
    nlohmann::json j;
    uint16_t val = 42;
    mapValidOrNull(j, "Key", &val);
    EXPECT_EQ(j["Key"], 42);
}

TEST(Uint16OrOmit, TombstoneOmitsKey)
{
    nlohmann::json j;
    uint16_t val = std::numeric_limits<uint16_t>::max();
    mapValidOrOmit(j, "Key", &val);
    EXPECT_FALSE(j.contains("Key"));
}

// =====================================================================
// Enum — mapValidOrNull / mapValidOrOmit
// =====================================================================

const EnumTranslator testTranslator =
    [](const std::string& val) -> std::optional<std::string> {
    if (val ==
        "xyz.openbmc_project.Inventory.Decorator.PortInfo.PortType.Ethernet")
    {
        return "Ethernet";
    }
    if (val ==
        "xyz.openbmc_project.Inventory.Decorator.PortInfo.PortType.Unknown")
    {
        return "";
    }
    return std::nullopt;
};

TEST(EnumOrNull, NullptrOmitsKey)
{
    nlohmann::json j;
    mapValidOrNull(j, "Key", static_cast<const std::string*>(nullptr),
                   testTranslator);
    EXPECT_FALSE(j.contains("Key"));
}

TEST(EnumOrNull, UnknownSetsNull)
{
    nlohmann::json j;
    std::string val =
        "xyz.openbmc_project.Inventory.Decorator.PortInfo.PortType.Unknown";
    mapValidOrNull(j, "Key", &val, testTranslator);
    EXPECT_TRUE(j.contains("Key"));
    EXPECT_TRUE(j["Key"].is_null());
}

TEST(EnumOrNull, UnsupportedOmitsKey)
{
    nlohmann::json j;
    std::string val =
        "xyz.openbmc_project.Inventory.Decorator.PortInfo.PortType.Unsupported";
    mapValidOrNull(j, "Key", &val, testTranslator);
    EXPECT_FALSE(j.contains("Key"));
}

TEST(EnumOrNull, ValidValueSetsTranslated)
{
    nlohmann::json j;
    std::string val =
        "xyz.openbmc_project.Inventory.Decorator.PortInfo.PortType.Ethernet";
    mapValidOrNull(j, "Key", &val, testTranslator);
    EXPECT_EQ(j["Key"], "Ethernet");
}

TEST(EnumOrOmit, NullptrOmitsKey)
{
    nlohmann::json j;
    mapValidOrOmit(j, "Key", static_cast<const std::string*>(nullptr),
                   testTranslator);
    EXPECT_FALSE(j.contains("Key"));
}

TEST(EnumOrOmit, UnknownOmitsKey)
{
    nlohmann::json j;
    std::string val =
        "xyz.openbmc_project.Inventory.Decorator.PortInfo.PortType.Unknown";
    mapValidOrOmit(j, "Key", &val, testTranslator);
    EXPECT_FALSE(j.contains("Key"));
}

TEST(EnumOrOmit, UnsupportedOmitsKey)
{
    nlohmann::json j;
    std::string val =
        "xyz.openbmc_project.Inventory.Decorator.PortInfo.PortType.Unsupported";
    mapValidOrOmit(j, "Key", &val, testTranslator);
    EXPECT_FALSE(j.contains("Key"));
}

TEST(EnumOrOmit, ValidValueSetsTranslated)
{
    nlohmann::json j;
    std::string val =
        "xyz.openbmc_project.Inventory.Decorator.PortInfo.PortType.Ethernet";
    mapValidOrOmit(j, "Key", &val, testTranslator);
    EXPECT_EQ(j["Key"], "Ethernet");
}

} // namespace
} // namespace redfish
