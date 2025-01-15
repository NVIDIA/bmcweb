/*
 * SPDX-FileCopyrightText: Copyright (c) 2024 NVIDIA CORPORATION & AFFILIATES.
 * All rights reserved. SPDX-License-Identifier: Apache-2.0
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
#include "utils/nsm_cmd_support.hpp"

#include "utils/json_utils.hpp"
#include "utils/memfd_utils.hpp"
#include "utils/nvidia_async_call_utils.hpp"

#include <error_messages.hpp>

namespace redfish
{
namespace nsm_command_support
{

void actionInfoResponse(const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                        const std::string& bmcId)
{
    asyncResp->res.jsonValue["@odata.type"] = "#ActionInfo.v1_1_2.ActionInfo";
    asyncResp->res.jsonValue["@odata.id"] =
        "/redfish/v1/Managers/" + bmcId + "/Oem/Nvidia/NSMRawCommandActionInfo";
    asyncResp->res.jsonValue["Name"] = "NSMRawCommand Action Info";
    asyncResp->res.jsonValue["Id"] = "NSMRawCommandActionInfo";
    asyncResp->res.jsonValue["Parameters"] = nlohmann::json::array(
        {{{"Name", "DeviceIdentificationId"},
          {"Required", true},
          {"DataType", "Number"},
          {"AllowableValues", {0, 1, 2, 3, 4}}},
         {{"Name", "DeviceInstanceId"},
          {"Required", true},
          {"DataType", "Number"}},
         {{"Name", "IsLongRunning"},
          {"Required", false},
          {"DataType", "Boolean"}},
         {{"Name", "MessageType"}, {"Required", true}, {"DataType", "Number"}},
         {{"Name", "CommandCode"}, {"Required", true}, {"DataType", "Number"}},
         {{"Name", "DataSizeBytes"},
          {"Required", true},
          {"DataType", "Number"},
          {"MinimumValue", 0},
          {"MaximumValue", 255}},
         {{"Name", "Data"},
          {"Required", false},
          {"DataType", "NumberArray"},
          {"MinimumValue", 0},
          {"MaximumValue", 255}}});
}

bool parseRequestJson(const crow::Request& req,
                      const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                      uint8_t& commandCode, uint8_t& deviceIdentificationId,
                      uint8_t& deviceInstanceId, uint8_t& messageType,
                      bool& isLongRunning, uint16_t& dataSizeInBytes,
                      std::vector<uint8_t>& data)
{
    std::optional<bool> optionalIsLongRunning;
    std::optional<std::vector<uint8_t>> optionalData;

    if (!redfish::json_util::readJsonAction(
            req, asyncResp->res, "CommandCode", commandCode, "Data",
            optionalData, "DataSizeBytes", dataSizeInBytes,
            "DeviceIdentificationId", deviceIdentificationId,
            "DeviceInstanceId", deviceInstanceId, "IsLongRunning",
            optionalIsLongRunning, "MessageType", messageType))
    {
        BMCWEB_LOG_ERROR("Failed to parse JSON body.");
        return false;
    }

    isLongRunning = optionalIsLongRunning.value_or(false);
    data = optionalData.value_or(std::vector<uint8_t>(dataSizeInBytes));
    return true;
}

static void parseResponse(const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                          uint8_t messageType, uint8_t commandCode,
                          const MemoryFD& fd)
{
    std::vector<uint8_t> responseData;
    try
    {
        fd.read(responseData);
    }
    catch (const std::exception& e)
    {
        BMCWEB_LOG_ERROR("Memory file descriptor read error: {}", e.what());
        messages::internalError(asyncResp->res);
        return;
    }
    asyncResp->res.jsonValue["MessageType"] = messageType;
    asyncResp->res.jsonValue["CommandCode"] = commandCode;
    if (responseData.size() < 1)
    {
        BMCWEB_LOG_ERROR("Send Nsm Raw Command response data is empty");
        messages::internalError(asyncResp->res);
        return;
    }
    uint8_t cc = responseData[0];
    asyncResp->res.jsonValue["CompletionCode"] = cc;
    if (cc != 0 && responseData.size() == 3)
    {
        uint16_t resonCode;
        memcpy(&resonCode, &responseData[1], sizeof(uint16_t));
        resonCode = le16toh(resonCode);
        asyncResp->res.jsonValue["ReasonCode"] = resonCode;
    }
    else if (responseData.size() > 1)
    {
        responseData.erase(responseData.begin());
        asyncResp->res.jsonValue["Data"] = responseData;
    }
    messages::success(asyncResp->res);
}

void callSendRequest(const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                     uint8_t deviceIdentificationId, uint8_t deviceInstanceId,
                     bool isLongRunning, uint8_t messageType,
                     uint8_t commandCode, const std::vector<uint8_t>& data)
{
    MemoryFD memFd;
    sdbusplus::message::unix_fd fd(memFd.fd);
    memFd.write(data);
    nvidia_async_operation_utils::doGenericCallAsyncAndGatherResult<uint8_t>(
        asyncResp, std::chrono::seconds(60), "xyz.openbmc_project.NSM",
        "/xyz/openbmc_project/NSM/Raw", "com.nvidia.Protocol.NSM.Raw",
        "SendRequest",
        [asyncResp, messageType, commandCode, memFd = std::move(memFd)](
            const std::string& status, [[maybe_unused]] const uint8_t* rc) {
        if (status == nvidia_async_operation_utils::asyncStatusValueSuccess)
        {
            parseResponse(asyncResp, messageType, commandCode, memFd);
            return;
        }
        BMCWEB_LOG_ERROR("Send Nsm Raw Command error {}", status);
        messages::internalError(asyncResp->res);
    },
        deviceIdentificationId, deviceInstanceId, isLongRunning, messageType,
        commandCode, fd);
}

} // namespace nsm_command_support
} // namespace redfish
