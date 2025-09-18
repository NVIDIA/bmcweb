// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: Copyright OpenBMC Authors
#pragma once

#include "async_resp.hpp"
#include "error_messages.hpp"
#include "nvidia_manager_utils.hpp"
#include "persistent_data.hpp"

#include <nlohmann/json.hpp>

#include <memory>
#include <optional>
#include <string_view>

namespace redfish
{

namespace manager_utils
{

inline void setServiceIdentification(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    std::string_view serviceIdentification)
{
    constexpr const size_t maxStrSize = 100;
    constexpr const char* allowedChars =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789 _-";

    if (serviceIdentification.size() > maxStrSize)
    {
        messages::stringValueTooLong(asyncResp->res, "ServiceIdentification",
                                     maxStrSize);
        return;
    }

    if (serviceIdentification.find_first_not_of(allowedChars) !=
        std::string::npos)
    {
        messages::propertyValueError(asyncResp->res, "ServiceIdentification");
        return;
    }

    persistent_data::ConfigFile& config = persistent_data::getConfig();
    config.serviceIdentification = serviceIdentification;
    config.writeData();
    messages::success(asyncResp->res);
}

inline void getServiceIdentification(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const bool isServiceRoot)
{
    std::string_view serviceIdentification =
        persistent_data::getConfig().serviceIdentification;

    // This property shall not be present if its value is an empty string or
    // null: Redfish Data Model Specification 6.125.3
    if (isServiceRoot && serviceIdentification.empty())
    {
        // This is working as designed, and empty string by
        // default is expected.
        // return;
    }
    asyncResp->res.jsonValue["ServiceIdentification"] = serviceIdentification;
}

} // namespace manager_utils

} // namespace redfish
