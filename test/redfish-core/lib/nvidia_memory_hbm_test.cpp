// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION &
// AFFILIATES. All rights reserved.

#include "bmcweb_config.h"

#include "nvidia_memory.hpp"

#include <nlohmann/json.hpp>

#include <cstddef>
#include <cstdint>

#include <gtest/gtest.h>

namespace redfish::nvidia_memory
{
namespace
{

// The values an HMC serves today for a GPU HBM DIMM that has no
// FormFactor / width / revision / attribute data on D-Bus.
nlohmann::json hbmWithPlaceholders()
{
    nlohmann::json memory;
    memory["MemoryDeviceType"] = "HBM";
    memory["MemoryType"] = "DRAM";
    memory["BaseModuleType"] = "RDIMM";
    memory["BusWidthBits"] = uint16_t{0};
    memory["DataWidthBits"] = uint16_t{0};
    memory["FirmwareRevision"] = "0";
    memory["RankCount"] = size_t{0};
    memory["CapacityMiB"] = size_t{190008};
    memory["ErrorCorrection"] = "SingleBitECC";
    return memory;
}

TEST(NvidiaMemoryTest, RemoveHbmDimmPlaceholdersDropsDefaults)
{
    nlohmann::json memory = hbmWithPlaceholders();
    removeHbmDimmPlaceholders(memory);

    EXPECT_FALSE(memory.contains("BaseModuleType"));
    EXPECT_FALSE(memory.contains("BusWidthBits"));
    EXPECT_FALSE(memory.contains("DataWidthBits"));
    EXPECT_FALSE(memory.contains("FirmwareRevision"));
    EXPECT_FALSE(memory.contains("RankCount"));

    EXPECT_EQ(memory["MemoryDeviceType"], "HBM");
    EXPECT_EQ(memory["MemoryType"], "DRAM");
    EXPECT_EQ(memory["CapacityMiB"], 190008);
    EXPECT_EQ(memory["ErrorCorrection"], "SingleBitECC");
}

TEST(NvidiaMemoryTest, RemoveHbmDimmPlaceholdersCoversHbmVersions)
{
    for (const char* type : {"HBM2", "HBM2E", "HBM3"})
    {
        nlohmann::json memory = hbmWithPlaceholders();
        memory["MemoryDeviceType"] = type;
        removeHbmDimmPlaceholders(memory);
        EXPECT_FALSE(memory.contains("BaseModuleType")) << type;
        EXPECT_FALSE(memory.contains("RankCount")) << type;
    }
}

TEST(NvidiaMemoryTest, RemoveHbmDimmPlaceholdersKeepsRealValues)
{
    nlohmann::json memory = hbmWithPlaceholders();
    memory["BaseModuleType"] = "Die";
    memory["BusWidthBits"] = uint16_t{1024};
    memory["DataWidthBits"] = uint16_t{1024};
    memory["FirmwareRevision"] = "3";
    memory["RankCount"] = size_t{2};
    const nlohmann::json expected = memory;

    removeHbmDimmPlaceholders(memory);

    EXPECT_EQ(memory, expected);
}

TEST(NvidiaMemoryTest, RemoveHbmDimmPlaceholdersLeavesDdrUnchanged)
{
    nlohmann::json memory = hbmWithPlaceholders();
    memory["MemoryDeviceType"] = "DDR5";
    const nlohmann::json expected = memory;

    removeHbmDimmPlaceholders(memory);

    EXPECT_EQ(memory, expected);
}

TEST(NvidiaMemoryTest, RemoveHbmDimmPlaceholdersNeedsDeviceType)
{
    nlohmann::json memory = hbmWithPlaceholders();
    memory.erase("MemoryDeviceType");
    const nlohmann::json expected = memory;

    removeHbmDimmPlaceholders(memory);
    EXPECT_EQ(memory, expected);

    nlohmann::json notObject = nlohmann::json::array();
    removeHbmDimmPlaceholders(notObject);
    EXPECT_EQ(notObject, nlohmann::json::array());
}

TEST(NvidiaMemoryTest, ApplyHbmDimmPlaceholderOptionFollowsBuildOption)
{
    nlohmann::json memory = hbmWithPlaceholders();
    applyHbmDimmPlaceholderOption(memory);

    EXPECT_EQ(memory.contains("BaseModuleType"),
              BMCWEB_MEMORY_HBM_DIMM_PLACEHOLDERS);
    EXPECT_EQ(memory.contains("RankCount"),
              BMCWEB_MEMORY_HBM_DIMM_PLACEHOLDERS);
    EXPECT_TRUE(memory.contains("CapacityMiB"));
}

} // namespace
} // namespace redfish::nvidia_memory
