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

#include "logging.hpp"

#include <boost/process.hpp>

#include <chrono>
#include <functional>
#include <iostream>
#include <string>

namespace bp = boost::process;
using LldpResponseCallback = std::function<void(
    const std::shared_ptr<bmcweb::AsyncResp>&, const std::string& /* stdOut*/,
    const std::string& /* stdErr*/, const boost::system::error_code& /* ec */,
    int /*errorCode */)>;

enum class LldpTlv
{
    CHASSIS_ID,
    PORT_ID,
    SYSTEM_CAPABILITIES,
    SYSTEM_DESCRIPTION,
    SYSTEM_NAME,
    MANAGEMENT_ADDRESS,
    ADMIN_STATUS,
    ENABLE_ADMIN_STATUS,
    DISABLE_ADMIN_STATUS,
    ALL
};

enum class LldpCommandType
{
    GET,
    GET_LLDP,
    SET_LLDP,
    ENABLE_TLV
};

class LldpToolUtil
{
  public:
    /**
     * @brief Execute lldptool commands
     * @param ifName - The interface name
     * @param lldpTlv - the Requested TLV type
     * @param lldpCommandType - The command type
     * @param isReceived - is the command for received TLV or for transmitted
     * TLV
     * @param asyncResp - Pointer to object holding response data.
     * @param responseCallback - callback function to handle the response.
     *
     * @return none.
     */
    inline static void run(const std::string& ifName, LldpTlv lldpTlv,
                           LldpCommandType lldpCommandType, bool isReceived,
                           const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                           LldpResponseCallback responseCallback);

  private:
    LldpToolUtil() = default;
    /**
     * @brief Translate to lldptool command
     * @param lldpTlv - the enum with commands available for lldptool tool.
     * @param ifName - the interface name
     * @param lldpCommandType - the command type
     * @param isReceived - is the command for received TLV or for transmitted
     * TLV
     *
     * @return string with the command to be executed.
     */
    inline static std::string
        translateOperationToCommand(const std::string& ifName, LldpTlv lldpTlv,
                                    LldpCommandType lldpCommandType,
                                    bool isReceived);
};

/**
 * @brief Translate to lldptool command
 * @param lldpTlv - the enum with commands available for lldptool tool.
 * @param ifName - the interface name
 * @param lldpCommandType - the command type
 * @param isReceived - is the command for received TLV or for transmitted TLV
 *
 * @return string with the command to be executed.
 */
inline std::string LldpToolUtil::translateOperationToCommand(
    const std::string& ifName, LldpTlv lldpTlv, LldpCommandType lldpCommandType,
    bool isReceived)
{
    std::string command;
    std::string cmdAction{" set-tlv "};
    std::string setRequest{""};
    std::string tlvRequest{""};
    std::string neighbor{""};

    switch (lldpCommandType)
    {
        case LldpCommandType::ENABLE_TLV:
            setRequest = " enableTx=yes";
            break;
        case LldpCommandType::GET:
            cmdAction = " get-tlv ";
            break;
        case LldpCommandType::GET_LLDP:
            cmdAction = " get-lldp ";
            break;
        case LldpCommandType::SET_LLDP:
            cmdAction = " set-lldp ";
            break;
    }

    switch (lldpTlv)
    {
        case LldpTlv::CHASSIS_ID:
            tlvRequest = " -V chassisID ";
            break;
        case LldpTlv::PORT_ID:
            tlvRequest = " -V portID ";
            break;
        case LldpTlv::SYSTEM_CAPABILITIES:
            tlvRequest = " -V sysCap ";
            break;
        case LldpTlv::SYSTEM_DESCRIPTION:
            tlvRequest = " -V sysDesc ";
            break;
        case LldpTlv::SYSTEM_NAME:
            tlvRequest = " -V sysName ";
            break;
        case LldpTlv::MANAGEMENT_ADDRESS:
            tlvRequest = " -V mngAddr ";
            break;
        case LldpTlv::ADMIN_STATUS:
            tlvRequest = " adminStatus ";
            break;
        case LldpTlv::ENABLE_ADMIN_STATUS:
            setRequest = " adminStatus=rxtx";
            break;
        case LldpTlv::DISABLE_ADMIN_STATUS:
            setRequest = " adminStatus=disabled";
            break;
        case LldpTlv::ALL:
            break;
    }

    if (isReceived)
    {
        neighbor = " -n ";
    }
    command = "lldptool " + cmdAction + neighbor + " -i " + ifName +
              tlvRequest + setRequest;
    BMCWEB_LOG_DEBUG("lldptool command: {}", command);
    return command;
}

inline void
    LldpToolUtil::run(const std::string& ifName, LldpTlv lldpTlv,
                      LldpCommandType lldpCommandType, bool isReceived,
                      const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                      LldpResponseCallback responseCallback)
{
    std::string command = translateOperationToCommand(
        ifName, lldpTlv, lldpCommandType, isReceived);
    auto dataOut = std::make_shared<boost::process::ipstream>();
    auto dataErr = std::make_shared<boost::process::ipstream>();
    auto exitCallback = [asyncResp, dataOut, dataErr,
                         respCallback = std::move(responseCallback),
                         command](const boost::system::error_code& ec,
                                  int errorCode) mutable {
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
            BMCWEB_LOG_ERROR("Error while executing command: {} Error Code {}",
                             command, errorCode);
            BMCWEB_LOG_ERROR("LLDP Error Response: {}", stdErr);
            if (ec)
            {
                BMCWEB_LOG_ERROR(
                    "Error while executing command: {} Message: {}", command,
                    ec.message());
            }
        }
        respCallback(asyncResp, stdOut, stdErr, ec, errorCode);
        return;
    };
    bp::async_system(crow::connections::systemBus->get_io_context(),
                     std::move(exitCallback), command, bp::std_in.close(),
                     bp::std_out > *dataOut, bp::std_err > *dataErr);
}
