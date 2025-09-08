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
#include "credential_pipe.hpp"

#include <boost/process/v2/process.hpp>
#include <boost/process/v2/stdio.hpp>
#include <boost/system/error_code.hpp>
#include <array>

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
    const crow::Request&, const std::shared_ptr<bmcweb::AsyncResp>&,
    uint32_t /* endpointId */, const std::string& /* stdOut*/,
    const std::string& /* stdErr*/, const boost::system::error_code& /* ec */,
    int /*errorCode */)>;

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
             const crow::Request& req,
             const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
             ResponseCallback responseCallback) const
    {
        translateOperationToCommand(mctpVdmUtilcommand, std::move(data));
        namespace bpv2 = boost::process::v2;
        auto& io = crow::connections::systemBus->get_io_context();

        struct ProcState
        {
            bpv2::process proc;
            std::array<char, 4096> outBuf{};
            std::array<char, 4096> errBuf{};
            std::string outStr;
            std::string errStr;
            int exitCode{0};
            bool outDone{false};
            bool errDone{false};
            bool waitDone{false};
            explicit ProcState(boost::asio::io_context& ioCtx) : proc(ioCtx) {}
        };

        auto state = std::make_shared<ProcState>(io);

        // Launch via shell to pass full command string
        state->proc = bpv2::process(
            io, "/bin/sh", std::vector<std::string>{"-c", command},
            bpv2::process_stdio{.in = bpv2::stdio::null,
                                 .out = bpv2::stdio::pipe,
                                 .err = bpv2::stdio::pipe});

        auto tryComplete = [state, &req, asyncResp, cb = std::move(responseCallback),
                            endpointId = this->endpointId]() mutable {
            if (state->outDone && state->errDone && state->waitDone)
            {
                boost::system::error_code ec;
                if (state->exitCode != 0)
                {
                    ec = make_error_code(boost::system::errc::io_error);
                }
                cb(req, asyncResp, endpointId, state->outStr, state->errStr, ec, state->exitCode);
            }
        };

        auto readOut = [state, tryComplete](auto& self) mutable {
            state->proc.stdout().async_read_some(boost::asio::buffer(state->outBuf),
                                       [state, &self, tryComplete](const boost::system::error_code& ec, std::size_t n) mutable {
                                           if (!ec)
                                           {
                                               state->outStr.append(state->outBuf.data(), n);
                                               self(self);
                                               return;
                                           }
                                           state->outDone = true;
                                           tryComplete();
                                       });
        };
        auto readErr = [state, tryComplete](auto& self) mutable {
            state->proc.stderr().async_read_some(boost::asio::buffer(state->errBuf),
                                       [state, &self, tryComplete](const boost::system::error_code& ec, std::size_t n) mutable {
                                           if (!ec)
                                           {
                                               state->errStr.append(state->errBuf.data(), n);
                                               self(self);
                                               return;
                                           }
                                           state->errDone = true;
                                           tryComplete();
                                       });
        };

        // Start reading and waiting
        readOut(readOut);
        readErr(readErr);
        state->proc.async_wait([state, tryComplete](const boost::system::error_code& /*ec*/, int code) mutable {
            state->exitCode = code;
            state->waitDone = true;
            tryComplete();
        });
    }
};
