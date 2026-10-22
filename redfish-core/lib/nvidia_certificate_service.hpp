// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: Copyright NVIDIA Authors
#pragma once

#include "async_resp.hpp"
#include "error_messages.hpp"
#include "logging.hpp"

#include <memory>
#include <string>
#include <string_view>

namespace redfish
{

inline bool handleNvidiaCertError(
    const std::string_view dbusErrorName, const std::string& reason,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp)
{
    if (dbusErrorName ==
        "xyz.openbmc_project.Certs.Error.CertificateLimitReached")
    {
        BMCWEB_LOG_ERROR("Certificate limit reached: {}", reason);
        messages::createLimitReachedForResource(asyncResp->res);
        return true;
    }
    if (dbusErrorName == "xyz.openbmc_project.Common.Error.NotAllowed")
    {
        BMCWEB_LOG_ERROR("Certificate Already exists: {}", reason);
        messages::resourceAlreadyExists(asyncResp->res, "Certificate",
                                        "CertificateString",
                                        "<posted certificate>");
        return true;
    }
    return false;
}

} // namespace redfish
