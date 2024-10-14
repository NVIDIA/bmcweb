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

#include <boost/process.hpp>

#include <chrono>
#include <functional>
#include <iostream>
#include <string>

enum class MctpVdmUtilCommand
{
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

namespace bp = boost::process;
using ResponseCallback = std::function<void(
    const crow::Request&, const std::shared_ptr<bmcweb::AsyncResp>&,
    uint32_t /* endpointId */, const std::string& /* stdOut*/,
    const std::string& /* stdErr*/, const boost::system::error_code& /* ec */,
    int /*errorCode */)>;

struct MctpVdmUtil
{
  private:
    void translateOperationToCommand(MctpVdmUtilCommand mctpVdmUtilcommand)
    {
        std::string cmd;

        switch (mctpVdmUtilcommand)
        {
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
        }

        command = "mctp-vdm-util -t " + std::to_string(endpointId) + " -c " +
                  cmd;
    }
    uint32_t endpointId = 0L;
    std::string command;

  public:
    MctpVdmUtil(uint32_t endpointId) : endpointId(endpointId) {}

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
    void run(MctpVdmUtilCommand mctpVdmUtilcommand, const crow::Request& req,
             const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
             ResponseCallback responseCallback)
    {
        translateOperationToCommand(mctpVdmUtilcommand);
        auto dataOut = std::make_shared<boost::process::ipstream>();
        auto dataErr = std::make_shared<boost::process::ipstream>();
        auto exitCallback =
            [req, asyncResp, dataOut, dataErr,
             respCallback = std::move(responseCallback),
             endpointId = this->endpointId, command = this->command](
                const boost::system::error_code& ec, int errorCode) mutable {
            std::string stdOut;
            while (*dataOut)
            {
                std::string line;
                std::getline(*dataOut, line);
                stdOut += line + "\n";
            }
            dataOut->close();
            std::string stdErr;
            while (*dataErr)
            {
                std::string line;
                std::getline(*dataErr, line);
                stdErr += line + "\n";
            }
            dataErr->close();
            if (ec || errorCode)
            {
                BMCWEB_LOG_ERROR(
                    "Error while executing command: {} Error Code: {}", command,
                    errorCode);
                BMCWEB_LOG_ERROR("MCTP VDM Error Response: {}", stdErr);
                if (ec)
                {
                    BMCWEB_LOG_ERROR(
                        "Error while executing command: {} Message: {}",
                        command, ec.message());
                }
            }
            respCallback(req, asyncResp, endpointId, stdOut, stdErr, ec,
                         errorCode);
            return;
        };
        bp::async_system(crow::connections::systemBus->get_io_context(),
                         std::move(exitCallback), command, bp::std_in.close(),
                         bp::std_out > *dataOut, bp::std_err > *dataErr);
    }
};
