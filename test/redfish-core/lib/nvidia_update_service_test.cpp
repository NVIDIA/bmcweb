// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES.
// All rights reserved.

#include "async_resp.hpp"
#include "commit_image.hpp"
#include "nvidia_update_service.hpp"

#include <unistd.h>

#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>
#include <ios>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include <gtest/gtest.h>

namespace redfish
{
namespace
{

class CommitImageAllowListTest : public ::testing::Test
{
  protected:
    void SetUp() override
    {
        path = (std::filesystem::temp_directory_path() /
                ("bmcweb_allowlist_" + std::to_string(getpid())))
                   .string();
        write("");
    }

    void TearDown() override
    {
        std::error_code ec;
        std::filesystem::remove(path, ec);
    }

    void write(std::string_view contents) const
    {
        std::ofstream out(path, std::ios::trunc);
        out << contents;
    }

    std::string path;
};

TEST_F(CommitImageAllowListTest, MissingFileIsNotConfigured)
{
    std::filesystem::remove(path);
    CommitImageAllowList list = loadCommitImageAllowList(path);
    EXPECT_EQ(list.state, CommitImageAllowListState::notConfigured);
    EXPECT_TRUE(list.values.empty());
}

TEST_F(CommitImageAllowListTest, EmptyFileIsNotConfigured)
{
    write(" \n");
    CommitImageAllowList list = loadCommitImageAllowList(path);
    EXPECT_EQ(list.state, CommitImageAllowListState::notConfigured);
}

TEST_F(CommitImageAllowListTest, NoEntriesIsNotConfigured)
{
    write(R"({"FwUuidMap":{}})");
    EXPECT_EQ(loadCommitImageAllowList(path).state,
              CommitImageAllowListState::notConfigured);
    write(R"({"Other":1})");
    EXPECT_EQ(loadCommitImageAllowList(path).state,
              CommitImageAllowListState::notConfigured);
}

TEST_F(CommitImageAllowListTest, MalformedJsonFailsClosed)
{
    write(R"({"FwUuidMap":)");
    CommitImageAllowList list = loadCommitImageAllowList(path);
    EXPECT_EQ(list.state, CommitImageAllowListState::loadFailed);
    EXPECT_TRUE(list.values.empty());
}

TEST_F(CommitImageAllowListTest, WrongShapeFailsClosed)
{
    write(R"([1,2])");
    EXPECT_EQ(loadCommitImageAllowList(path).state,
              CommitImageAllowListState::loadFailed);
    write(R"({"FwUuidMap":["a"]})");
    EXPECT_EQ(loadCommitImageAllowList(path).state,
              CommitImageAllowListState::loadFailed);
    write(R"({"FwUuidMap":{"/a":1}})");
    EXPECT_EQ(loadCommitImageAllowList(path).state,
              CommitImageAllowListState::loadFailed);
}

TEST_F(CommitImageAllowListTest, UnreadableFileFailsClosed)
{
    std::filesystem::remove(path);
    std::filesystem::create_directory(path);
    EXPECT_EQ(loadCommitImageAllowList(path).state,
              CommitImageAllowListState::loadFailed);
    std::filesystem::remove(path);
}

TEST_F(CommitImageAllowListTest, EntriesAreLoaded)
{
    write(R"({"FwUuidMap":{"/xyz/a":"uuid-a","/xyz/b":2,"/xyz/c":"uuid-c"}})");
    CommitImageAllowList list = loadCommitImageAllowList(path);
    EXPECT_EQ(list.state, CommitImageAllowListState::loaded);
    ASSERT_EQ(list.values.size(), 2U);
    EXPECT_EQ(list.values[0].inventoryUri, "/xyz/a");
    EXPECT_EQ(list.values[0].uuid, "uuid-a");
    EXPECT_EQ(list.values[1].inventoryUri, "/xyz/c");
}

TEST(CommitImageInfoFromImageCopy, ListsEachFirmwareOnce)
{
    auto asyncResp = std::make_shared<bmcweb::AsyncResp>();
    std::map<std::string, ChassisInfo> chassisMap;
    chassisMap["A"].softwarePaths = {"/xyz/openbmc_project/software/fw_b",
                                     "/xyz/openbmc_project/software/fw_a"};
    chassisMap["B"].softwarePaths = {"/xyz/openbmc_project/software/fw_a", "/"};

    nvidia::updateParametersForCommitImageInfoFromImageCopy(
        asyncResp, chassisMap);

    const nlohmann::json& params = asyncResp->res.jsonValue["Parameters"];
    ASSERT_EQ(params.size(), 1U);
    EXPECT_EQ(params[0]["Name"], "Targets");
    EXPECT_EQ(params[0]["AllowableValues"],
              nlohmann::json::array(
                  {"/redfish/v1/UpdateService/FirmwareInventory/fw_a",
                   "/redfish/v1/UpdateService/FirmwareInventory/fw_b"}));
}

} // namespace
} // namespace redfish
