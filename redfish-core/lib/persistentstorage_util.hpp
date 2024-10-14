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
#include "error_messages.hpp"

#include <boost/process.hpp>

#include <functional>
#include <iostream>
#include <string>

namespace redfish
{

namespace bp = boost::process;

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
static std::unordered_map<ExitCode, ErrorMapping> emmcServiceErrorMapping = {
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
inline std::optional<ErrorMapping>
    getEMMCErrorMessageFromExitCode(ExitCode exitCode)
{
    if (emmcServiceErrorMapping.find(exitCode) != emmcServiceErrorMapping.end())
    {
        return emmcServiceErrorMapping[exitCode];
    }
    else
    {
        BMCWEB_LOG_ERROR("No mapping found for ExitCode: {}", exitCode);
        return std::nullopt;
    }
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
    void executeEnvCommand(const crow::Request& req,
                           const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                           const std::string& command,
                           AsyncResponseCallback responseCallback)
    {
        auto dataOut = std::make_shared<boost::process::ipstream>();
        auto dataErr = std::make_shared<boost::process::ipstream>();
        auto exitCallback = [&req, asyncResp, dataOut, dataErr, command,
                             respCallback = std::move(responseCallback)](
                                const boost::system::error_code& ec,
                                int errorCode) mutable {
            std::string stdOut;
            std::string stdErr;
            if (ec || errorCode)
            {
                BMCWEB_LOG_ERROR(
                    "Error while executing persistent storage command: {} Error Code: {}",
                    command, errorCode);

                while (*dataErr)
                {
                    std::string line;
                    std::getline(*dataErr, line);
                    stdErr += line + "\n";
                }
                dataErr->close();
                BMCWEB_LOG_ERROR("Command Response: {}", stdErr);
                if (ec)
                {
                    BMCWEB_LOG_ERROR(
                        "Error while executing command: {} Message: {}",
                        command, ec.message());
                }
                return;
            }
            while (*dataOut)
            {
                std::string line;
                std::getline(*dataOut, line);
                stdOut += line + "\n";
            }
            dataOut->close();
            respCallback(req, asyncResp, stdOut, stdErr, ec, errorCode);
            return;
        };
        bp::async_system(crow::connections::systemBus->get_io_context(),
                         exitCallback, command, bp::std_in.close(),
                         bp::std_out > *dataOut, bp::std_err > *dataErr);
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
        [asyncResp](const boost::system::error_code ec,
                    const std::variant<int32_t>& property) {
        if (ec)
        {
            BMCWEB_LOG_ERROR("DBUS response error getting service status: {}",
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
                .jsonValue["Oem"]["Nvidia"]["PersistentStorageSettings"]
                          ["Status"]["State"] = "Enabled";
        }
        else if (*serviceStatus == eudaProgrammedNotActivated)
        {
            asyncResp->res
                .jsonValue["Oem"]["Nvidia"]["PersistentStorageSettings"]
                          ["Status"]["State"] = "StandbyOffline";
        }
        else
        {
            asyncResp->res
                .jsonValue["Oem"]["Nvidia"]["PersistentStorageSettings"]
                          ["Status"]["State"] = "Disabled";
        }
    },
        "org.freedesktop.systemd1",
        "/org/freedesktop/systemd1/unit/nvidia_2demmc_2dpartition_2eservice",
        "org.freedesktop.DBus.Properties", "Get",
        "org.freedesktop.systemd1.Service", "ExecMainStatus");
}

} // namespace redfish
