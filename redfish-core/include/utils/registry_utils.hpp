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
/*!
 * @file    registry_utils.cpp
 * @brief   Source code for utility functions of handling message registries.
 */

#pragma once

#include <boost/algorithm/string/split.hpp>
#include <registries.hpp>

#include <array>
#include <cstddef>
#include <cstring>
#include <span>
#include <string>
#include <utility>

namespace redfish
{
namespace message_registries
{

inline std::string getPrefix(const std::string& messageID)
{
    size_t pos = messageID.find('.');
    return messageID.substr(0, pos);
}

inline bool isMessageIdValid(const std::string_view messageId)
{
    const redfish::registries::Message* msg =
        redfish::registries::getMessage(messageId);
    (void)msg;
    return msg != nullptr;
}

inline void
    updateResolution(const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                     const std::string& property, std::string resolution)
{
    std::string extendInfo = property + "@Message.ExtendedInfo";
    auto& extendedInfoArr = asyncResp->res.jsonValue[extendInfo];
    if (extendedInfoArr.size() > 0)
    {
        std::string oldResolution = extendedInfoArr[0]["Resolution"];
        resolution = oldResolution + resolution;
        extendedInfoArr[0]["Resolution"] = resolution;
    }
}

inline void updateMessageSeverity(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& property, const std::string& messageSeverity)
{
    std::string extendInfo = property + "@Message.ExtendedInfo";
    auto& extendedInfoArr = asyncResp->res.jsonValue[extendInfo];
    if (extendedInfoArr.size() > 0)
    {
        extendedInfoArr[0]["MessageSeverity"] = messageSeverity;
    }
}

inline std::string composeMessage(const std::string& messageId,
                                  const nlohmann::json& messageArgs)
{
    const registries::Message* msg = registries::getMessage(messageId);

    if (msg == nullptr)
    {
        BMCWEB_LOG_ERROR("MessageId[{}] not supported", messageId);
        return "";
    }

    std::string message = msg->message;
    int i = 0;
    for (auto& arg : messageArgs)
    {
        // Substituion
        std::string argStr = "%" + std::to_string(++i);
        size_t argPos = message.find(argStr);
        if (argPos != std::string::npos)
        {
            message.replace(argPos, argStr.length(), arg);
        }
    }

    return message;
}

} // namespace message_registries
} // namespace redfish
