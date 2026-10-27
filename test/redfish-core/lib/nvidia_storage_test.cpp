// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION &
// AFFILIATES. All rights reserved.

#include "nvidia_storage.hpp"

#include <optional>
#include <string>

#include <gtest/gtest.h>

namespace redfish
{
namespace
{

TEST(NvidiaStorageTest, ConvertDriveOperationMapsKnownOperations)
{
    EXPECT_EQ(convertDriveOperation(
                  "xyz.openbmc_project.Nvme.Operation.OperationType.Sanitize"),
              std::optional<std::string>("Sanitize"));
    EXPECT_EQ(convertDriveOperation(
                  "xyz.openbmc_project.Nvme.Operation.OperationType.Format"),
              std::optional<std::string>("Format"));
}

TEST(NvidiaStorageTest, ConvertDriveOperationNoOperationIsNullopt)
{
    EXPECT_EQ(
        convertDriveOperation(
            "xyz.openbmc_project.Nvme.Operation.OperationType.NoOperation"),
        std::nullopt);
}

TEST(NvidiaStorageTest, ConvertDriveOperationUnknownIsUnchanged)
{
    EXPECT_EQ(convertDriveOperation(""), std::optional<std::string>(""));
    EXPECT_EQ(convertDriveOperation("not.a.valid.OperationType"),
              std::optional<std::string>(""));
}

} // namespace
} // namespace redfish
