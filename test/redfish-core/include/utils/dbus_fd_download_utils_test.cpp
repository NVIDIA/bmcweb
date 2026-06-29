/*
 * SPDX-FileCopyrightText: Copyright (c) 2021-2026 NVIDIA CORPORATION &
 * AFFILIATES. All rights reserved. SPDX-License-Identifier: Apache-2.0
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "async_resp.hpp"
#include "dbus_singleton.hpp"
#include "task.hpp"
#include "utils/dbus_fd_download_utils.hpp"

#include <unistd.h>

#include <boost/asio/io_context.hpp>
#include <boost/beast/http/field.hpp>
#include <boost/beast/http/status.hpp>
#include <boost/system/error_code.hpp>
#include <sdbusplus/asio/connection.hpp>
#include <sdbusplus/message/native_types.hpp>

#include <array>
#include <filesystem>
#include <memory>
#include <string>

#include <gtest/gtest.h>

namespace redfish::dbus_fd_utils
{
namespace
{

class ProcessProgressPropertiesTest : public ::testing::Test
{
  protected:
    boost::asio::io_context ioc;
    std::unique_ptr<sdbusplus::asio::connection> conn;

    void SetUp() override
    {
        conn = std::make_unique<sdbusplus::asio::connection>(ioc);
        crow::connections::systemBus = conn.get();
    }

    void TearDown() override
    {
        task::TaskRegistry::getInstance().getTasks().clear();
        crow::connections::systemBus = nullptr;
    }

    static std::shared_ptr<task::TaskData> createTestTask()
    {
        return task::TaskData::createTask(
            [](boost::system::error_code, sdbusplus::message_t&,
               const std::shared_ptr<task::TaskData>&) { return false; },
            "");
    }

    static dbus::utility::DBusPropertiesMap makeStatusProps(
        const std::string& status)
    {
        dbus::utility::DBusPropertiesMap props;
        props.emplace_back("Status", dbus::utility::DbusVariantType(status));
        return props;
    }

    static dbus::utility::DBusPropertiesMap makeProgressProps(uint8_t progress)
    {
        dbus::utility::DBusPropertiesMap props;
        props.emplace_back("Progress",
                           dbus::utility::DbusVariantType(progress));
        return props;
    }
};

TEST_F(ProcessProgressPropertiesTest, ProgressUpdatesPercentComplete)
{
    std::shared_ptr<task::TaskData> taskData = createTestTask();
    dbus::utility::DBusPropertiesMap props = makeProgressProps(50);

    bool result = processProgressProperties(props, taskData);

    EXPECT_FALSE(result);
    EXPECT_EQ(taskData->percentComplete, 50);
}

TEST_F(ProcessProgressPropertiesTest, InProgressReturnsNotCompleted)
{
    std::shared_ptr<task::TaskData> taskData = createTestTask();
    dbus::utility::DBusPropertiesMap props = makeStatusProps(
        "xyz.openbmc_project.Common.Progress.OperationStatus.InProgress");

    bool result = processProgressProperties(props, taskData);

    EXPECT_FALSE(result);
    EXPECT_EQ(taskData->state, "Running");
}

TEST_F(ProcessProgressPropertiesTest, CompletedSetsStateAndMessage)
{
    std::shared_ptr<task::TaskData> taskData = createTestTask();
    dbus::utility::DBusPropertiesMap props = makeStatusProps(
        "xyz.openbmc_project.Common.Progress.OperationStatus.Completed");

    bool result = processProgressProperties(props, taskData);

    EXPECT_TRUE(result);
    EXPECT_EQ(taskData->state, "Completed");
    EXPECT_EQ(taskData->percentComplete, 100);
    EXPECT_FALSE(taskData->messages.empty());
}

TEST_F(ProcessProgressPropertiesTest, AbortedSetsException)
{
    std::shared_ptr<task::TaskData> taskData = createTestTask();
    dbus::utility::DBusPropertiesMap props = makeStatusProps(
        "xyz.openbmc_project.Common.Progress.OperationStatus.Aborted");

    bool result = processProgressProperties(props, taskData);

    EXPECT_TRUE(result);
    EXPECT_EQ(taskData->state, "Exception");
    EXPECT_EQ(taskData->percentComplete, 100);
}

TEST_F(ProcessProgressPropertiesTest, FailedSetsException)
{
    std::shared_ptr<task::TaskData> taskData = createTestTask();
    dbus::utility::DBusPropertiesMap props = makeStatusProps(
        "xyz.openbmc_project.Common.Progress.OperationStatus.Failed");

    bool result = processProgressProperties(props, taskData);

    EXPECT_TRUE(result);
    EXPECT_EQ(taskData->state, "Exception");
    EXPECT_EQ(taskData->percentComplete, 100);
}

TEST_F(ProcessProgressPropertiesTest, UnexpectedStatusSetsException)
{
    std::shared_ptr<task::TaskData> taskData = createTestTask();
    dbus::utility::DBusPropertiesMap props =
        makeStatusProps("xyz.openbmc_project.Common.Progress.bogus");

    bool result = processProgressProperties(props, taskData);

    EXPECT_TRUE(result);
    EXPECT_EQ(taskData->state, "Exception");
    EXPECT_EQ(taskData->percentComplete, 100);
}

TEST_F(ProcessProgressPropertiesTest, NoStatusOrProgressReturnsNotCompleted)
{
    std::shared_ptr<task::TaskData> taskData = createTestTask();
    dbus::utility::DBusPropertiesMap props;

    bool result = processProgressProperties(props, taskData);

    EXPECT_FALSE(result);
    EXPECT_EQ(taskData->state, "Running");
}

TEST_F(ProcessProgressPropertiesTest, WrongPropertyTypeReturnsNotCompleted)
{
    std::shared_ptr<task::TaskData> taskData = createTestTask();
    dbus::utility::DBusPropertiesMap props;
    props.emplace_back("Status", dbus::utility::DbusVariantType(uint8_t(1)));

    bool result = processProgressProperties(props, taskData);

    EXPECT_FALSE(result);
    EXPECT_FALSE(taskData->messages.empty());
}

TEST_F(ProcessProgressPropertiesTest, BothProgressAndStatusProcessed)
{
    std::shared_ptr<task::TaskData> taskData = createTestTask();
    dbus::utility::DBusPropertiesMap props;
    props.emplace_back("Progress",
                       dbus::utility::DbusVariantType(uint8_t(100)));
    props.emplace_back(
        "Status",
        dbus::utility::DbusVariantType(std::string(
            "xyz.openbmc_project.Common.Progress.OperationStatus.Completed")));

    bool result = processProgressProperties(props, taskData);

    EXPECT_TRUE(result);
    EXPECT_EQ(taskData->state, "Completed");
    EXPECT_EQ(taskData->percentComplete, 100);
    EXPECT_FALSE(taskData->messages.empty());
}

TEST_F(ProcessProgressPropertiesTest,
       HandleTaskMessage_ErrorCodeAddsInternalError)
{
    std::shared_ptr<task::TaskData> taskData = createTestTask();
    boost::system::error_code ec =
        boost::system::errc::make_error_code(boost::system::errc::io_error);
    // msg is not accessed when ec is set; default-constructed is safe here.
    sdbusplus::message_t msg;

    bool result = handleTaskMessage(ec, msg, taskData);

    EXPECT_TRUE(result);
    EXPECT_FALSE(taskData->messages.empty());
}

TEST(StreamFdResponseTest, EnoentReturns404)
{
    auto asyncResp = std::make_shared<bmcweb::AsyncResp>();
    boost::system::error_code ec = boost::system::errc::make_error_code(
        boost::system::errc::no_such_file_or_directory);
    sdbusplus::message::unix_fd fd{-1};

    streamFdResponse(asyncResp, ec, fd);

    EXPECT_EQ(asyncResp->res.result(), boost::beast::http::status::not_found);
}

TEST(StreamFdResponseTest, HostUnreachableReturns404)
{
    auto asyncResp = std::make_shared<bmcweb::AsyncResp>();
    boost::system::error_code ec = boost::system::errc::make_error_code(
        boost::system::errc::host_unreachable);
    sdbusplus::message::unix_fd fd{-1};

    streamFdResponse(asyncResp, ec, fd);

    EXPECT_EQ(asyncResp->res.result(), boost::beast::http::status::not_found);
}

TEST(StreamFdResponseTest, OtherErrorReturns500)
{
    auto asyncResp = std::make_shared<bmcweb::AsyncResp>();
    boost::system::error_code ec =
        boost::system::errc::make_error_code(boost::system::errc::io_error);
    sdbusplus::message::unix_fd fd{-1};

    streamFdResponse(asyncResp, ec, fd);

    EXPECT_EQ(asyncResp->res.result(),
              boost::beast::http::status::internal_server_error);
}

TEST(StreamFdResponseTest, SuccessWithValidFdSetsContentType)
{
    auto asyncResp = std::make_shared<bmcweb::AsyncResp>();
    boost::system::error_code ec;

    std::array<int, 2> pipefd{};
    ASSERT_EQ(::pipe(pipefd.data()), 0);
    sdbusplus::message::unix_fd fd{pipefd[0]};

    streamFdResponse(asyncResp, ec, fd);

    EXPECT_EQ(asyncResp->res.result(), boost::beast::http::status::ok);
    EXPECT_EQ(
        asyncResp->res.getHeaderValue(boost::beast::http::field::content_type),
        "application/octet-stream");

    ::close(pipefd[0]);
    ::close(pipefd[1]);
}

TEST(StreamFdResponseTest, InvalidFdReturns500)
{
    auto asyncResp = std::make_shared<bmcweb::AsyncResp>();
    boost::system::error_code ec;
    sdbusplus::message::unix_fd fd{-1};

    streamFdResponse(asyncResp, ec, fd);

    EXPECT_EQ(asyncResp->res.result(),
              boost::beast::http::status::internal_server_error);
}

size_t countOpenFds()
{
    size_t count = 0;
    for (const auto& entry :
         std::filesystem::directory_iterator("/proc/self/fd"))
    {
        (void)entry;
        ++count;
    }
    return count;
}

TEST(StreamFdResponseTest, InvalidFdDoesNotLeakFd)
{
    size_t before = countOpenFds();

    auto asyncResp = std::make_shared<bmcweb::AsyncResp>();
    boost::system::error_code ec;
    sdbusplus::message::unix_fd fd{-1};

    streamFdResponse(asyncResp, ec, fd);

    EXPECT_EQ(countOpenFds(), before);
}

TEST(StreamFdResponseTest, ErrorPathDoesNotLeakFd)
{
    size_t before = countOpenFds();

    auto asyncResp = std::make_shared<bmcweb::AsyncResp>();
    boost::system::error_code ec =
        boost::system::errc::make_error_code(boost::system::errc::io_error);
    sdbusplus::message::unix_fd fd{-1};

    streamFdResponse(asyncResp, ec, fd);

    EXPECT_EQ(countOpenFds(), before);
}

TEST(StreamFdResponseTest, SuccessPathDoesNotLeakFd)
{
    std::array<int, 2> pipefd{};
    ASSERT_EQ(::pipe(pipefd.data()), 0);

    size_t before = countOpenFds();

    {
        auto asyncResp = std::make_shared<bmcweb::AsyncResp>();
        boost::system::error_code ec;
        sdbusplus::message::unix_fd fd{pipefd[0]};

        streamFdResponse(asyncResp, ec, fd);
    }

    // After asyncResp is destroyed, the duped fd should be closed.
    // Only the original pipe fds should remain.
    EXPECT_EQ(countOpenFds(), before);

    ::close(pipefd[0]);
    ::close(pipefd[1]);
}

} // namespace
} // namespace redfish::dbus_fd_utils
