// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: Copyright OpenBMC Authors
// SPDX-FileCopyrightText: Copyright 2018 Intel Corporation
#pragma once

#include "json_utils.hpp"

namespace redfish
{

namespace json_util
{

template <typename Type>
bool getValueFromJsonObject(nlohmann::json& jsonData, const std::string& key,
                            Type& value)
{
    nlohmann::json::iterator it = jsonData.find(key);
    if (it == jsonData.end())
    {
        BMCWEB_LOG_DEBUG("Key {} not exist", key);
        return false;
    }

    return details::unpackValue(*it, key, value);
}

template <typename FirstType, typename... UnpackTypes>
bool readJsonSubObject(nlohmann::json::object_t& jsonRequest,
                       crow::Response& res, std::string_view key,
                       FirstType&& first, UnpackTypes&&... in)
{
    const std::size_t n = sizeof...(UnpackTypes) + 2;
    std::array<PerUnpack, n / 2> toUnpack2;
    packVariant(toUnpack2, key, std::forward<FirstType>(first),
                std::forward<UnpackTypes&&>(in)...);
    // Only validate the keys we care about, ignore extra keys
    return readJsonHelperObject(jsonRequest, res, toUnpack2,
                                /*allowUnknownKeys=*/true);
}

template <typename FirstType, typename... UnpackTypes>
bool readJsonSub(nlohmann::json& jsonRequest, crow::Response& res,
                 std::string_view key, FirstType&& first, UnpackTypes&&... in)
{
    nlohmann::json::object_t* obj =
        jsonRequest.get_ptr<nlohmann::json::object_t*>();
    if (obj == nullptr)
    {
        BMCWEB_LOG_DEBUG("Json value is not an object");
        messages::unrecognizedRequestBody(res);
        return false;
    }
    return readJsonSubObject(*obj, res, key, std::forward<FirstType>(first),
                             std::forward<UnpackTypes&&>(in)...);
}

} // namespace json_util
} // namespace redfish