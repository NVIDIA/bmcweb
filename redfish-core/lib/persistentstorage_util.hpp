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
#include "error_messages.hpp"

#include <boost/asio/connect_pipe.hpp>
#include <boost/asio/readable_pipe.hpp>
#include <boost/asio/writable_pipe.hpp>
#include <boost/process/v2/process.hpp>
#include <boost/process/v2/stdio.hpp>

#include <functional>
#include <iostream>
#include <string>

namespace redfish
{

using ExitCode = int32_t;
using ErrorMessage = std::string;
using Resolution = std::string;
using ErrorMapping = std::pair<ErrorMessage, Resolution>;

/* Exist codes returned by nvidia-emmc partition service after completion.*/
enum EMMCServiceExitCodes
{
    emmcPartitionMounted = 0,
    emmcInitFail = 1,
    emmcDisabled = 2,
    eudaProgramFail = 3,
    eudaProgrammedNotActivated = 4,
    emmcPartitionFail = 5,
    emmcFileSystemFormatFail = 6,
    emmcMountFail = 7
};

/* EMMC Service error mapping */
static const std::unordered_map<ExitCode, ErrorMapping>
    emmcServiceErrorMapping = {
        {emmcInitFail,
         {"PersistentStorage Initialization Failure",
          "Reset the baseboard and retry the operation."}},
        {eudaProgramFail,
         {"PersistentStorage Configuration Failure", "Retry the operation."}},
        {eudaProgrammedNotActivated,
         {"PersistentStorage Enabled but not activated",
          "Reset the baseboard to activate the PersistentStorage."}},
        {emmcPartitionFail,
         {"PersistentStorage Internal Error: Partition Fail",
          "Reset the baseboard and retry the operation."}},
        {emmcFileSystemFormatFail,
         {"PersistentStorage Internal Error: File System Format Failure",
          "Reset the baseboard and retry the operation."}},
        {emmcMountFail,
         {"PersistentStorage Internal Error: Mount Failure",
          "Reset the baseboard and retry the operation."}},
};

/**
 * @brief get EMMC error message from service exit code
 *
 * @param[in] exitCode
 * @return std::optional<ErrorMapping>
 */
inline std::optional<ErrorMapping> getEMMCErrorMessageFromExitCode(
    ExitCode exitCode)
{
    if (emmcServiceErrorMapping.contains(exitCode))
    {
        auto it = emmcServiceErrorMapping.find(exitCode);
        return it->second;
    }

    BMCWEB_LOG_ERROR("No mapping found for ExitCode: {}", exitCode);
    return std::nullopt;
}

using AsyncResponseCallback = std::function<void(
    const crow::Request&, const std::shared_ptr<bmcweb::AsyncResp>&,
    const std::string& /* stdOut*/, const std::string& /* stdErr*/,
    const boost::system::error_code& /* ec */, int /*errorCode */)>;

struct PersistentStorageUtil
{
  public:
    /**
     * @brief updates persistent storage enabled property by reading the uboot
     * env variable
     *
     * @param req
     * @param asyncResp
     * @param command
     * @param respCallback
     */
    static void executeEnvCommand(
        const crow::Request& req,
        const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
        const std::string& command, AsyncResponseCallback responseCallback)
    {
        namespace bpv2 = boost::process::v2;
        auto& io = crow::connections::systemBus->get_io_context();

        struct State
        {
            std::shared_ptr<bpv2::process> proc;
            std::unique_ptr<boost::asio::readable_pipe> outRead;
            std::unique_ptr<boost::asio::readable_pipe> errRead;
            std::array<char, 4096> outBuf{};
            std::array<char, 4096> errBuf{};
            std::string stdOut;
            std::string stdErr;
            int exitCode{0};
            bool outDone{false};
            bool errDone{false};
            bool waitDone{false};
        };

        auto state = std::make_shared<State>();
        boost::asio::readable_pipe outRead(io);
        boost::asio::writable_pipe outWrite(io);
        boost::asio::connect_pipe(outRead, outWrite);
        boost::asio::readable_pipe errRead(io);
        boost::asio::writable_pipe errWrite(io);
        boost::asio::connect_pipe(errRead, errWrite);
        state->outRead =
            std::make_unique<boost::asio::readable_pipe>(std::move(outRead));
        state->errRead =
            std::make_unique<boost::asio::readable_pipe>(std::move(errRead));

        auto tryComplete = [state, &req, asyncResp,
                            cb = std::move(responseCallback), command](
                               const boost::system::error_code& ec) mutable {
            if (state->outDone && state->errDone && state->waitDone)
            {
                if (ec || state->exitCode != 0)
                {
                    BMCWEB_LOG_ERROR(
                        "Error while executing persistent storage command: {} Error Code: {}",
                        command, state->exitCode);
                    if (!state->stdErr.empty())
                    {
                        BMCWEB_LOG_ERROR("Command Response: {}", state->stdErr);
                    }
                    if (ec)
                    {
                        BMCWEB_LOG_ERROR(
                            "Error while executing command: {} Message: {}",
                            command, ec.message());
                    }
                    return;
                }
                cb(req, asyncResp, state->stdOut, state->stdErr, ec,
                   state->exitCode);
            }
        };

        // Launch via shell so command string is honored
        state->proc = std::make_shared<bpv2::process>(
            io, "/bin/sh", std::vector<std::string>{"-c", command},
            bpv2::process_stdio{.in = nullptr,
                                .out = std::move(outWrite),
                                .err = std::move(errWrite)});

        // Read stdout
        std::function<void()> readOut;
        readOut = [state, tryComplete, &readOut]() mutable {
            state->outRead->async_read_some(
                boost::asio::buffer(state->outBuf),
                [state, tryComplete,
                 &readOut](const boost::system::error_code& ec,
                           std::size_t n) mutable {
                    if (!ec)
                    {
                        state->stdOut.append(state->outBuf.data(), n);
                        readOut();
                        return;
                    }
                    state->outDone = true;
                    tryComplete(ec);
                });
        };
        readOut();

        // Read stderr
        std::function<void()> readErr;
        readErr = [state, tryComplete, &readErr]() mutable {
            state->errRead->async_read_some(
                boost::asio::buffer(state->errBuf),
                [state, tryComplete,
                 &readErr](const boost::system::error_code& ec,
                           std::size_t n) mutable {
                    if (!ec)
                    {
                        state->stdErr.append(state->errBuf.data(), n);
                        readErr();
                        return;
                    }
                    state->errDone = true;
                    tryComplete(ec);
                });
        };
        readErr();

        // Wait for process exit
        state->proc->async_wait(
            [state, tryComplete](const boost::system::error_code& ec,
                                 int code) mutable {
                state->exitCode = code;
                state->waitDone = true;
                tryComplete(ec);
            });
    }
};

/**
 * @brief populate Status.State property based on EMMC service exit code
 *
 * @param asyncResp - Pointer to object holding response data.
 *
 * @return None.
 */
inline void populatePersistentStorageSettingStatus(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp)
{
    crow::connections::systemBus->async_method_call(
        [asyncResp](const boost::system::error_code& ec,
                    const std::variant<int32_t>& property) {
            if (ec)
            {
                BMCWEB_LOG_ERROR(
                    "DBUS response error getting service status: {}",
                    ec.message());
                redfish::messages::internalError(asyncResp->res);
                return;
            }
            const int32_t* serviceStatus = std::get_if<int32_t>(&property);
            if (serviceStatus == nullptr)
            {
                BMCWEB_LOG_ERROR("Invalid service exit status code");
                redfish::messages::internalError(asyncResp->res);
                return;
            }
            if (*serviceStatus == emmcPartitionMounted)
            {
                asyncResp->res
                    .jsonValue["PersistentStorageSettings"]["Status"]["State"] =
                    "Enabled";
            }
            else if (*serviceStatus == eudaProgrammedNotActivated)
            {
                asyncResp->res
                    .jsonValue["PersistentStorageSettings"]["Status"]["State"] =
                    "StandbyOffline";
            }
            else
            {
                asyncResp->res
                    .jsonValue["PersistentStorageSettings"]["Status"]["State"] =
                    "Disabled";
            }
        },
        "org.freedesktop.systemd1",
        "/org/freedesktop/systemd1/unit/nvidia_2demmc_2dpartition_2eservice",
        "org.freedesktop.DBus.Properties", "Get",
        "org.freedesktop.systemd1.Service", "ExecMainStatus");
}

} // namespace redfish
