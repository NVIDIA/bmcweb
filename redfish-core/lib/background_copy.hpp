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
#include "mctp_vdm_util_wrapper.hpp"

#include <regex>
/**
 *@brief updates AutomaticBackgroundCopyEnabled property
 *
 * @param req - Pointer to object holding request data
 * @param asyncResp - Pointer to object holding response data
 * @param endpointId the EID which is used
 * by mctp-vdm-util tool to call request on MCTP
 * @param allowList - List containing allowable chassis Ids
 * @param chassisId - chassisId
 * @param[in] callback - A callback function to be called after update
 *AutomaticBackgroundCopyEnabled property
 * @return
 */
inline void updateBackgroundCopyEnabled(
    const crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp, uint32_t endpointId,
    const auto& allowList, const std::string& chassisId,
    const std::function<void()>& callback = {})
{
    MctpVdmUtil mctpVdmUtilWrapper(endpointId);

    auto responseCallback =
        [callback]([[maybe_unused]] const crow::Request& req,
                   const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                   [[maybe_unused]] uint32_t endpointId,
                   const std::string& stdOut,
                   [[maybe_unused]] const std::string& stdErr,
                   const boost::system::error_code& ec, int errorCode) -> void {
        if (ec || errorCode)
        {
            return;
        }
        nlohmann::json& oem = asyncResp->res.jsonValue["Oem"]["Nvidia"];

        std::string rxTemplate = "(.|\n)*RX:( \\d\\d){9} 01(.|\n)*";
        if (std::regex_match(stdOut, std::regex(rxTemplate)))
        {
            oem["AutomaticBackgroundCopyEnabled"] = true;
        }
        else
        {
            oem["AutomaticBackgroundCopyEnabled"] = false;
        }

        if (callback)
        {
            callback();
        }

        return;
    };
    if (std::find(allowList.begin(), allowList.end(), chassisId) ==
        allowList.end())
    {
        if (callback)
        {
            callback();
        }
    }
    else
    {
        mctpVdmUtilWrapper.run(MctpVdmUtilCommand::BACKGROUNDCOPY_STATUS,
                               std::monostate(), req, asyncResp,
                               responseCallback);
    }

    return;
}

/**
 *@brief Updates status of background copy property pending or completed.
 *
 * @param req - Pointer to object holding request data
 * @param asyncResp - Pointer to object holding response data
 * @param endpointId the EID which is used by mctp-vdm-util tool to call request
 *on MCTP
 * @param allowList - List containing allowable chassis Ids
 * @param chassisId - chassisId
 * @param[in] callback - A callback function to be called after update
 *BackgroundCopyStatus property
 *
 * @return
 */
inline void updateBackgroundCopyStatusPending(
    const crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp, uint32_t endpointId,
    const auto& allowList, const std::string& chassisId,
    const std::function<void()>& callback = {})
{
    MctpVdmUtil mctpVdmUtilWrapper(endpointId);
    auto bgCopyQueryResponseCallback =
        [callback]([[maybe_unused]] const crow::Request& req,
                   const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                   [[maybe_unused]] uint32_t endpointId,
                   const std::string& stdOut,
                   [[maybe_unused]] const std::string& stdErr,
                   const boost::system::error_code& ec, int errorCode) -> void {
        if (ec || errorCode)
        {
            return;
        }
        std::string rxTemplatePending = "(.|\n)*RX:( \\d\\d){9} 02(.|\n)*";
        nlohmann::json& oem = asyncResp->res.jsonValue["Oem"]["Nvidia"];
        if (std::regex_match(stdOut, std::regex(rxTemplatePending)))
        {
            oem["BackgroundCopyStatus"] = "Pending";
        }
        else
        {
            oem["BackgroundCopyStatus"] = "Completed";
        }

        if (callback)
        {
            callback();
        }

        return;
    };
    if (std::find(allowList.begin(), allowList.end(), chassisId) ==
        allowList.end())
    {
        if (callback)
        {
            callback();
        }
    }
    else
    {
    mctpVdmUtilWrapper.run(MctpVdmUtilCommand::BACKGROUNDCOPY_QUERY_PENDING,
                               std::monostate(), req, asyncResp,
                           std::move(bgCopyQueryResponseCallback));
    }

    return;
}

/**
 *@brief Updates status of background copy property
 *
 * @param req - Pointer to object holding request data
 * @param asyncResp - Pointer to object holding response data
 * @param endpointId the EID which is used
 * by mctp-vdm-util tool to call request on MCTP
 * @param allowList - List containing allowable chassis Ids
 * @param chassisId - chassisId
 * @param[in] callback - A callback function to be called after update
 *BackgroundCopyStatus property
 *
 * @return
 */
inline void updateBackgroundCopyStatus(
    const crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp, uint32_t endpointId,
    const auto& allowList, const std::string& chassisId,
    const std::function<void()>& callback = {})
{
    MctpVdmUtil mctpVdmUtilWrapper(endpointId);
    auto bgCopyQueryResponseCallback =
        [callback, allowList, chassisId](
            [[maybe_unused]] const crow::Request& req,
                   const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
            [[maybe_unused]] uint32_t endpointId, const std::string& stdOut,
                   [[maybe_unused]] const std::string& stdErr,
                   const boost::system::error_code& ec, int errorCode) -> void {
        if (ec || errorCode)
        {
            redfish::messages::internalError(asyncResp->res);
            return;
        }

        std::string rxTemplateCompleted = "(.|\n)*RX:( \\d\\d){9} 01(.|\n)*";
        std::string rxTemplateInProgress = "(.|\n)*RX:( \\d\\d){9} 02(.|\n)*";
        if (std::regex_match(stdOut, std::regex(rxTemplateCompleted)))
        {
            updateBackgroundCopyStatusPending(req, asyncResp, endpointId,
                                              allowList, chassisId);
        }
        else if (std::regex_match(stdOut, std::regex(rxTemplateInProgress)))
        {
            nlohmann::json& oem = asyncResp->res.jsonValue["Oem"]["Nvidia"];
            oem["BackgroundCopyStatus"] = "InProgress";

            if (callback)
            {
                callback();
            }
        }
        else
        {
            BMCWEB_LOG_ERROR(
                "Invalid response for background_copy_query_progress: {}",
                stdOut);
        }
        return;
    };

    if (std::find(allowList.begin(), allowList.end(), chassisId) ==
        allowList.end())
    {
        if (callback)
        {
            callback();
        }
    }
    else
    {
        mctpVdmUtilWrapper.run(
            MctpVdmUtilCommand::BACKGROUNDCOPY_QUERY_PROGRESS, std::monostate(),
                           req, asyncResp, bgCopyQueryResponseCallback);
    }
    return;
}

/**
 *@brief Enable or Disable background copy
 *
 * @param req - Pointer to object holding request data
 * @param asyncResp - Pointer to object holding response data
 * @param endpointId the EID which is used
 * by mctp-vdm-util tool to call request on MCTP
 * @param enabled Enable or disable the background copy
 * @param chassisID Chassis Id
 *
 * @return exit code form mctp-vdm-tool.
 */
inline void enableBackgroundCopy(
    const crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp, uint32_t endpointId,
    bool enabled, const std::string& chassisId)
{
    MctpVdmUtil mctpVdmUtilWrapper(endpointId);

    auto responseCallback =
        [enabled, chassisId](
            [[maybe_unused]] const crow::Request& req,
            const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
            [[maybe_unused]] uint32_t endpointId,
            [[maybe_unused]] const std::string& stdOut,
            [[maybe_unused]] const std::string& stdErr,
            const boost::system::error_code& ec, int errorCode) -> void {
        if (ec || errorCode)
        {
            const std::string errorMessage =
                (enabled == true)
                    ? "MCTP Command Failure: Background Copy Enable"
                    : "MCTP Command Failure: Background Copy Disable";

            redfish::messages::resourceErrorsDetectedFormatError(
                asyncResp->res, "/redfish/v1/Chassis/" + chassisId,
                errorMessage);
            return;
        }
        if (asyncResp->res.jsonValue.empty())
        {
            redfish::messages::success(asyncResp->res);
        }
    };

    if (enabled)
    {
        mctpVdmUtilWrapper.run(MctpVdmUtilCommand::BACKGROUNDCOPY_ENABLE,
                               std::monostate(), req, asyncResp,
                               responseCallback);
    }
    else
    {
        mctpVdmUtilWrapper.run(MctpVdmUtilCommand::BACKGROUNDCOPY_DISABLE,
                               std::monostate(), req, asyncResp,
                               responseCallback);
    }
    return;
}

/**
 *@brief Execute backgroundcopy_init command
 *
 * @param req - Pointer to object holding request data
 * @param asyncResp - Pointer to object holding response data
 * @param endpointId the EID which is used
 * @param inventoryURI - inventory uri
 * by mctp-vdm-util tool to call request on MCTP
 *
 * @return exit code form mctp-vdm-tool.
 */
inline void
    initBackgroundCopy(const crow::Request& req,
                       const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                       uint32_t endpointId, const std::string& inventoryURI)
{
    MctpVdmUtil mctpVdmUtilWrapper(endpointId);
    auto responseCallback =
        [inventoryURI](
            [[maybe_unused]] const crow::Request& req,
            const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
            [[maybe_unused]] uint32_t endpointId,
            [[maybe_unused]] const std::string& stdOut,
            [[maybe_unused]] const std::string& stdErr,
            const boost::system::error_code& ec, int errorCode) -> void {
        if (ec || errorCode)
        {
            redfish::messages::resourceErrorsDetectedFormatError(
                asyncResp->res, inventoryURI,
                "MCTP Command Failure: Background Copy Init");
            // remove ExtendedInfo messages which contains success message entry
            // in the response
            if (asyncResp->res.jsonValue.contains("@Message.ExtendedInfo"))
            {
                asyncResp->res.jsonValue.erase("@Message.ExtendedInfo");
            }
            return;
        }
        // update success if message is response is not filled
        if (asyncResp->res.jsonValue.empty())
        {
            redfish::messages::success(asyncResp->res);
        }
    };
    BMCWEB_LOG_INFO("Initializing background init for inventoryURI:{}",
                    inventoryURI);
    mctpVdmUtilWrapper.run(MctpVdmUtilCommand::BACKGROUNDCOPY_INIT,
                           std::monostate(), req, asyncResp, responseCallback);
    return;
}
