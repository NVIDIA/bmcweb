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

#include <async_resp.hpp>
#include <nlohmann/json.hpp>
#include <sdbusplus/asio/connection.hpp>

#include <memory>

namespace redfish
{
namespace nsm_command_support
{

void actionInfoResponse(const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                        const std::string& bmcId);

bool parseRequestJson(const crow::Request& req,
                      const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                      uint8_t& commandCode, uint8_t& deviceIdentificationId,
                      uint8_t& deviceInstanceId, uint8_t& messageType,
                      bool& isLongRunning, uint16_t& dataSizeInBytes,
                      std::vector<uint8_t>& data);

void callSendRequest(const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                     uint8_t deviceIdentificationId, uint8_t deviceInstanceId,
                     bool isLongRunning, uint8_t messageType,
                     uint8_t commandCode, const std::vector<uint8_t>& data);

} // namespace nsm_command_support
} // namespace redfish
