// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: Copyright OpenBMC Authors
// SPDX-FileCopyrightText: Copyright 2018 Intel Corporation

#pragma once

#include "bmcweb_config.h"

#include "async_resp.hpp"
#include "boost_formatters.hpp"
#include "error_messages.hpp"
#include "http_body.hpp"
#include "http_response.hpp"
#include "logging.hpp"

#include <asm-generic/errno.h>
#include <unistd.h>

#include <boost/beast/http/field.hpp>
#include <boost/beast/http/status.hpp>
#include <boost/beast/http/verb.hpp>
#include <boost/system/linux_error.hpp>
#include <boost/url/format.hpp>
#include <boost/url/url.hpp>
#include <sdbusplus/message.hpp>
#include <sdbusplus/message/native_types.hpp>
#include <sdbusplus/unpack_properties.hpp>

#include <cstdio>
#include <string>

namespace redfish
{
namespace log_services_utils
{

// NVIDIA code starts for streaming
// Default max size of 20MB to accommodate BMC/System dumps
constexpr long long int defaultMaxFileSize = 20LL * 1024LL * 1024LL;
// FDR dumps can be much larger; allow up to 2GB
constexpr long long int fdrMaxFileSize = 2LL * 1024LL * 1024LL * 1024LL;
// NVIDIA code ends for streaming

inline bool checkSizeLimit(
    int fd, crow::Response& res,
    // NVIDIA code for streaming: added maxFileSize parameter
    long long int maxFileSize = defaultMaxFileSize)
{
    long long int size = lseek(fd, 0, SEEK_END);
    if (size <= 0)
    {
        BMCWEB_LOG_ERROR("Failed to get size of file, lseek() returned {}",
                         size);
        messages::internalError(res);
        return false;
    }

    if (size > maxFileSize)
    {
        BMCWEB_LOG_ERROR("File size {} exceeds maximum allowed size of {}",
                         size, maxFileSize);
        messages::internalError(res);
        return false;
    }
    off_t rc = lseek(fd, 0, SEEK_SET);
    if (rc < 0)
    {
        BMCWEB_LOG_ERROR("Failed to reset file offset to 0");
        messages::internalError(res);
        return false;
    }
    return true;
}

inline void downloadEntryCallback(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& entryID, const std::string& downloadEntryType,
    const boost::system::error_code& ec,
    const sdbusplus::message::unix_fd& unixfd)
{
    if (ec.value() == EBADR)
    {
        messages::resourceNotFound(asyncResp->res, "EntryAttachment", entryID);
        return;
    }
    if (ec)
    {
        BMCWEB_LOG_ERROR("DBUS response error: {}", ec);
        messages::internalError(asyncResp->res);
        return;
    }

    // Make sure we know how to process the retrieved entry attachment
    // NVIDIA code starts here
    if ((downloadEntryType != "BMC") && (downloadEntryType != "System") &&
        (downloadEntryType != "FDR"))
    // NVIDIA code ends here
    {
        BMCWEB_LOG_ERROR("downloadEntryCallback() invalid entry type: {}",
                         downloadEntryType);
        messages::internalError(asyncResp->res);
        return;
    }

    int fd = -1;
    fd = dup(unixfd);
    if (fd < 0)
    {
        BMCWEB_LOG_ERROR("Failed to open file");
        messages::internalError(asyncResp->res);
        return;
    }
    // NVIDIA code starts for streaming
    long long int maxFileSize =
        (downloadEntryType == "FDR") ? fdrMaxFileSize : defaultMaxFileSize;
    if (!checkSizeLimit(fd, asyncResp->res, maxFileSize))
    // NVIDIA code ends for streaming
    {
        close(fd);
        return;
    }
    if (downloadEntryType == "System")
    {
        if constexpr (BMCWEB_SYSTEM_DUMP_BASE64_ENCODE)
        {
            if (!asyncResp->res.openFd(fd, bmcweb::EncodingType::Base64))
            {
                messages::internalError(asyncResp->res);
                close(fd);
                return;
            }
            asyncResp->res.addHeader("Content-Transfer-Encoding", "Base64");
            return;
        }
    }
    // NVIDIA code starts here
    if (downloadEntryType == "FDR")
    {
        if (!asyncResp->res.openFd(fd, bmcweb::EncodingType::Raw))
        {
            messages::internalError(asyncResp->res);
            close(fd);
            return;
        }
        return;
    }
    // NVIDIA code ends here
    if (!asyncResp->res.openFd(fd))
    {
        messages::internalError(asyncResp->res);
        close(fd);
        return;
    }
    asyncResp->res.addHeader(boost::beast::http::field::content_type,
                             "application/octet-stream");
}
} // namespace log_services_utils
} // namespace redfish
