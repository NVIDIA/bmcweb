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

#include "utils/mctp_utils.hpp"

#include <boost/interprocess/streams/bufferstream.hpp>
#include <boost/process.hpp>
#include <boost/process/async.hpp>
#include <boost/process/child.hpp>

#include <memory>
#include <numeric>
#include <vector>

namespace redfish
{

namespace dot
{

constexpr const size_t mctpVdmUtilOutputSize = 256;

enum class DotMctpVdmUtilCommand
{
    CAKInstall,
    CAKLock,
    CAKTest,
    DOTDisable,
    DOTTokenInstall,
};

static std::map<const DotMctpVdmUtilCommand, const std::string> commandsMap{
    {DotMctpVdmUtilCommand::CAKInstall, "cak_install"},
    {DotMctpVdmUtilCommand::CAKLock, "cak_lock"},
    {DotMctpVdmUtilCommand::CAKTest, "cak_test"},
    {DotMctpVdmUtilCommand::DOTDisable, "dot_disable"},
    {DotMctpVdmUtilCommand::DOTTokenInstall, "dot_token_install"}};

using ResultCallback = std::function<void(
    const std::string& /* data received from the MCTP endpoint */)>;
using ErrorCallback = std::function<void(
    const std::string& /* resource / procedure associated with the error */,
    const std::string& /* error message */)>;
class DotCommandHandler
{
  public:
    DotCommandHandler(const std::string& erot,
                      const DotMctpVdmUtilCommand command,
                      const std::vector<uint8_t>& data,
                      ResultCallback&& resultCallback,
                      ErrorCallback&& errorCallback, int timeoutSec = 3) :
        resCallback(resultCallback), errCallback(errorCallback)
    {
        mctp_utils::enumerateMctpEndpoints(
            [this, command, data, timeoutSec](
                const std::shared_ptr<std::vector<mctp_utils::MctpEndpoint>>&
                    endpoints) {
                if (endpoints && endpoints->size() != 0)
                {
                    runCommand(endpoints->begin()->getMctpEid(), command, data,
                               timeoutSec);
                }
                else
                {
                    errCallback("Endpoint enumeration", "no endpoints found");
                }
            },
            [this](bool critical, const std::string& desc,
                   const std::string& msg) {
                if (critical)
                {
                    errCallback(desc, msg);
                }
            },
            erot);
    }

  private:
    ResultCallback resCallback;
    ErrorCallback errCallback;

    std::unique_ptr<boost::process::child> subprocess;
    std::unique_ptr<boost::asio::steady_timer> subprocessTimer;
    std::vector<char> subprocessOutput;

    void subprocessExitCallback(int exitCode, const std::error_code& ec)
    {
        const std::string desc = "mctp-vdm-util execution exit callback";
        BMCWEB_LOG_DEBUG("{}", desc);
        if (ec)
        {
            errCallback(desc, ec.message());
        }
        else if (exitCode == 0)
        {
            boost::interprocess::bufferstream outputStream(
                subprocessOutput.data(), subprocessOutput.size());
            std::string line, rxLine, txLine;
            std::string output;
            while (std::getline(outputStream, line) && output.empty())
            {
                if (line.rfind("RX: ", 0) == 0)
                {
                    rxLine = line.substr(4);
                    BMCWEB_LOG_DEBUG(" RX: {}", rxLine);
                }
                if (line.rfind("TX: ", 0) == 0)
                {
                    txLine = line.substr(4);
                    BMCWEB_LOG_DEBUG(" TX: {}", txLine);
                }
                if (!rxLine.empty() && !txLine.empty())
                {
                    output = rxLine;
                }
            }
            if (output.empty())
            {
                errCallback(desc, "no RX data found");
            }
            else
            {
                resCallback(rxLine);
            }
        }
        else
        {
            errCallback(desc, "Exit code: " + std::to_string(exitCode));
        }
    }

    void runCommand(const int eid, const DotMctpVdmUtilCommand command,
                    const std::vector<uint8_t>& data, const int timeout)
    {
        const std::string desc = "mctp-vdm-util execution";
        BMCWEB_LOG_DEBUG("{}", desc);
        subprocessTimer = std::make_unique<boost::asio::steady_timer>(
            crow::connections::systemBus->get_io_context());
        subprocessTimer->expires_after(std::chrono::seconds(timeout));
        subprocessTimer->async_wait(
            [this, desc](const boost::system::error_code ec) {
                if (ec && ec != boost::asio::error::operation_aborted)
                {
                    if (subprocess)
                    {
                        subprocess.reset(nullptr);
                        errCallback(desc, "Timeout");
                    }
                }
            });

        std::vector<std::string> args = {"-c", commandsMap[command], "-t",
                                         std::to_string(eid)};
        for (const auto& byte : data)
        {
            std::stringstream ss;
            ss << std::hex << std::uppercase << std::setw(2)
               << std::setfill('0') << static_cast<int>(byte);
            args.emplace_back(ss.str());
        }
        BMCWEB_LOG_DEBUG("mctp-vdm-util {}", boost::algorithm::join(args, " "));
        try
        {
            size_t hexDataSize = data.size() * 3; // hex representation + space
            subprocessOutput.resize(mctpVdmUtilOutputSize + hexDataSize);
            auto callback = [this](int exitCode, const std::error_code& ec) {
                subprocessExitCallback(exitCode, ec);
            };
            subprocess = std::make_unique<boost::process::child>(
                "/usr/bin/mctp-vdm-util", args,
                boost::process::std_err > boost::process::null,
                boost::process::std_out > boost::asio::buffer(subprocessOutput),
                crow::connections::systemBus->get_io_context(),
                boost::process::on_exit = std::move(callback));
        }
        catch (const std::runtime_error& e)
        {
            errCallback(desc, e.what());
        }
    }

    void cleanup()
    {
        subprocessOutput.resize(0);
        subprocess.reset(nullptr);
        subprocessTimer.reset(nullptr);
    }
};

} // namespace dot

} // namespace redfish
