/*
 * SPDX-FileCopyrightText: Copyright (c) 2021-2024 NVIDIA CORPORATION &
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

#pragma once

#include "async_resp.hpp"
#include "dbus_singleton.hpp"
#include "logging.hpp"

#include <boost/asio/buffer.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/readable_pipe.hpp>
#include <boost/asio/writable_pipe.hpp>
#include <boost/asio/write.hpp>
#include <boost/process/v2/execute.hpp>
#include <boost/process/v2/process.hpp>
#include <boost/process/v2/stdio.hpp>

#include <chrono>
#include <functional>
#include <iostream>
#include <string>
#include <variant>
#include <vector>

constexpr const size_t mctpVdmUtilErrorCodeOffset = 8;

enum class MctpVdmUtilCommand
{
    /*debug token*/
    DEBUG_TOKEN_INSTALL,
    DEBUG_TOKEN_ERASE,
    DEBUG_TOKEN_QUERY,
    /*background_copy*/
    BACKGROUNDCOPY_INIT,
    BACKGROUNDCOPY_DISABLE,
    BACKGROUNDCOPY_ENABLE,
    BACKGROUNDCOPY_DISABLE_ONE,
    BACKGROUNDCOPY_ENABLE_ONE,
    BACKGROUNDCOPY_STATUS,
    BACKGROUNDCOPY_QUERY_PROGRESS,
    BACKGROUNDCOPY_QUERY_PENDING,
    /*in_band*/
    INBAND_DISABLE,
    INBAND_ENABLE,
    INBAND_STATUS,
    /*manual boot mode*/
    BOOTMODE_ENABLE,
    BOOTMODE_DISABLE,
    BOOTMODE_QUERY,
    BOOT_AP
};

struct MctpVdmUtilStatusResponse
{
    bool isSuccess{false};
    bool enabled{false};
};

struct MctpVdmUtilProgressStatusResponse
{
    bool isSuccess{false};
    std::string status;
};

using ResponseCallback = std::function<void(
    const std::shared_ptr<bmcweb::AsyncResp>&, uint32_t /* endpointId */,
    const std::string& /* stdOut*/, const std::string& /* stdErr*/,
    const boost::system::error_code& /* ec */, int /*errorCode */)>;

using MctpVdmUtilData = std::variant<std::monostate, std::vector<uint8_t>>;

struct MctpVdmUtil
{
  private:
    void translateOperationToCommand(MctpVdmUtilCommand mctpVdmUtilcommand,
                                     MctpVdmUtilData&& data) const
    {
        std::string cmd;

        switch (mctpVdmUtilcommand)
        {
            case MctpVdmUtilCommand::DEBUG_TOKEN_INSTALL:
                cmd = "debug_token_install";
                break;
            case MctpVdmUtilCommand::DEBUG_TOKEN_ERASE:
                cmd = "debug_token_erase";
                break;
            case MctpVdmUtilCommand::DEBUG_TOKEN_QUERY:
                cmd = "debug_token_query";
                break;

            case MctpVdmUtilCommand::BACKGROUNDCOPY_INIT:
                cmd = "background_copy_init";
                break;
            case MctpVdmUtilCommand::BACKGROUNDCOPY_DISABLE:
                cmd = "background_copy_disable";
                break;
            case MctpVdmUtilCommand::BACKGROUNDCOPY_ENABLE:
                cmd = "background_copy_enable";
                break;
            case MctpVdmUtilCommand::BACKGROUNDCOPY_DISABLE_ONE:
                cmd = "background_copy_disable_one";
                break;
            case MctpVdmUtilCommand::BACKGROUNDCOPY_ENABLE_ONE:
                cmd = "background_copy_enable_one";
                break;
            case MctpVdmUtilCommand::BACKGROUNDCOPY_STATUS:
                cmd = "background_copy_query_status";
                break;
            case MctpVdmUtilCommand::BACKGROUNDCOPY_QUERY_PROGRESS:
                cmd = "background_copy_query_progress";
                break;
            case MctpVdmUtilCommand::BACKGROUNDCOPY_QUERY_PENDING:
                cmd = "background_copy_query_pending";
                break;

            case MctpVdmUtilCommand::INBAND_DISABLE:
                cmd = "in_band_disable";
                break;
            case MctpVdmUtilCommand::INBAND_ENABLE:
                cmd = "in_band_enable";
                break;
            case MctpVdmUtilCommand::INBAND_STATUS:
                cmd = "in_band_query_status";
                break;

            case MctpVdmUtilCommand::BOOTMODE_ENABLE:
                cmd = "enable_boot_mode";
                break;
            case MctpVdmUtilCommand::BOOTMODE_DISABLE:
                cmd = "disable_boot_mode";
                break;
            case MctpVdmUtilCommand::BOOTMODE_QUERY:
                cmd = "query_boot_mode";
                break;
            case MctpVdmUtilCommand::BOOT_AP:
                cmd = "boot_ap";
                break;
            default:
                cmd = "";
                break;
        }

        command = "mctp-vdm-util -t " + std::to_string(endpointId) + " -c " +
                  cmd;
        auto movedData = std::move(data);
        std::vector<uint8_t>* vectorData =
            std::get_if<std::vector<uint8_t>>(&movedData);
        if (vectorData != nullptr)
        {
            std::stringstream ss;
            for (const auto& byte : *vectorData)
            {
                ss << " " << std::hex << std::setw(2) << std::setfill('0')
                   << static_cast<int>(byte);
            }
            command += ss.str();
            return;
        }
    }
    uint32_t endpointId = 0L;
    mutable std::string command;

  public:
    explicit MctpVdmUtil(uint32_t endpointIdIn) : endpointId(endpointIdIn) {}

    /**
     *@brief Execute mctp-vdm-util tool command for
     * relevant MCTP EID
     * @param mctpVdmUtilcommand the enum with commands available for
     *mctp-vdm-util tool.
     * @param req - Pointer to object holding request data.
     * @param asyncResp - Pointer to object holding response data.
     * @param responseCallback - callback function to handle the response.
     *
     * @return none.
     */
    void run(MctpVdmUtilCommand mctpVdmUtilcommand, MctpVdmUtilData data,
             const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
             ResponseCallback responseCallback) const
    {
        translateOperationToCommand(mctpVdmUtilcommand, std::move(data));
        namespace bpv2 = boost::process::v2;
        auto& io = crow::connections::systemBus->get_io_context();

        // Create pipes for stdout/stderr
        auto outPipe = std::make_shared<boost::asio::readable_pipe>(io);
        auto errPipe = std::make_shared<boost::asio::readable_pipe>(io);

        // Buffers and accumulators
        auto outBuf = std::make_shared<std::array<char, 4096>>();
        auto errBuf = std::make_shared<std::array<char, 4096>>();
        auto stdOutStr = std::make_shared<std::string>();
        auto stdErrStr = std::make_shared<std::string>();

        // Setup stdio redirection
        bpv2::process_stdio stdio{
            nullptr,  // stdin closed
            *outPipe, // stdout
            *errPipe  // stderr
        };

        // Launch process
        auto proc = std::make_shared<bpv2::process>(
            io, "/bin/sh", std::vector<std::string>{"-c", this->command},
            stdio);

        // Async readers
        auto readOut = [outPipe, outBuf, stdOutStr](auto&& self) -> void {
            outPipe->async_read_some(
                boost::asio::buffer(*outBuf),
                [outPipe, outBuf, stdOutStr,
                 self](const boost::system::error_code& ec,
                       std::size_t n) mutable {
                    if (!ec && n > 0)
                    {
                        stdOutStr->append(outBuf->data(), n);
                        self(self); // continue reading
                    }
                });
        };
        auto readErr = [errPipe, errBuf, stdErrStr](auto&& self) -> void {
            errPipe->async_read_some(
                boost::asio::buffer(*errBuf),
                [errPipe, errBuf, stdErrStr,
                 self](const boost::system::error_code& ec,
                       std::size_t n) mutable {
                    if (!ec && n > 0)
                    {
                        stdErrStr->append(errBuf->data(), n);
                        self(self); // continue reading
                    }
                });
        };

        readOut(readOut);
        readErr(readErr);

        // Completion callback
        proc->async_wait(
            [asyncResp, proc, outPipe, errPipe, stdOutStr, stdErrStr,
             respCallback = std::move(responseCallback),
             endpointId = this->endpointId, command = this->command](
                const boost::system::error_code& ec, int exitCode) mutable {
                if (ec || exitCode != 0)
                {
                    BMCWEB_LOG_ERROR(
                        "Error while executing command: {} Error Code: {}",
                        command, exitCode);
                    BMCWEB_LOG_ERROR("MCTP VDM Error Response: {}", *stdErrStr);
                    if (ec)
                    {
                        BMCWEB_LOG_ERROR(
                            "Error while executing command: {} Message: {}",
                            command, ec.message());
                    }
                }
                respCallback(asyncResp, endpointId, *stdOutStr, *stdErrStr, ec,
                             exitCode);
            });
    }
};
