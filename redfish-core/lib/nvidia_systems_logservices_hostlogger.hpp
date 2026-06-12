// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: Copyright 2025 NVIDIA Corporation
#pragma once

#include "app.hpp"
#include "async_resp.hpp"
#include "dbus_utility.hpp"
#include "error_messages.hpp"
#include "http_response.hpp"
#include "logging.hpp"

#include <unistd.h>

#include <boost/beast/http/field.hpp>
#include <boost/beast/http/status.hpp>
#include <boost/beast/http/verb.hpp>
#include <boost/url/format.hpp>
#include <sdbusplus/message/native_types.hpp>

#include <array>
#include <string>
#include <string_view>

namespace redfish
{

constexpr std::string_view archiveInterface =
    "xyz.openbmc_project.Console.Archive";
constexpr std::string_view archiveMethod = "GetLog";
constexpr std::string_view archiveObjectPath = "/xyz/openbmc_project/console";

/**
 * @brief Callback for handling archive download from D-Bus unixfd
 *
 * @param asyncResp - Async response object
 * @param ec - Error code from D-Bus call
 * @param msg - D-Bus message object (allows access to error details)
 */
inline void downloadArchiveCallback(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const boost::system::error_code& ec, sdbusplus::message::message& msg)
{
    if (ec)
    {
        if (ec.value() == EBADR)
        {
            BMCWEB_LOG_DEBUG(
                "Archive service not available (EBADR), returning without error");
            return;
        }

        const sd_bus_error* dbusError = msg.get_error();
        if (dbusError != nullptr && dbusError->name != nullptr)
        {
            std::string_view errorName(dbusError->name);
            if (errorName ==
                "xyz.openbmc_project.Console.Archive.Error.NoLogFilesFound")
            {
                BMCWEB_LOG_ERROR(
                    "No log files found, returning ResourceNotFound");
                messages::resourceNotFound(asyncResp->res, "LogService",
                                           "HostLogger");
                return;
            }
        }
        BMCWEB_LOG_ERROR("D-Bus GetLog() call failed: {}", ec);
        messages::internalError(asyncResp->res);
        return;
    }

    sdbusplus::message::unix_fd unixfd{};
    try
    {
        msg.read(unixfd);
    }
    catch (const sdbusplus::exception::SdBusError& e)
    {
        BMCWEB_LOG_ERROR("Failed to read unixfd from D-Bus message: {}",
                         e.what());
        messages::internalError(asyncResp->res);
        return;
    }

    int fd = dup(unixfd);
    if (fd < 0)
    {
        BMCWEB_LOG_ERROR("Failed to duplicate file descriptor: {}", errno);
        messages::internalError(asyncResp->res);
        return;
    }

    if (!asyncResp->res.openFd(DuplicatableFileHandle(fd)))
    {
        BMCWEB_LOG_ERROR("Failed to open file descriptor for streaming");
        close(fd);
        messages::internalError(asyncResp->res);
        return;
    }

    asyncResp->res.addHeader(boost::beast::http::field::content_type,
                             "application/gzip");
    asyncResp->res.addHeader(boost::beast::http::field::content_disposition,
                             "attachment; filename=\"console_logs.tar.gz\"");
}

/**
 * @brief Callback for when archive service is discovered - initiates download
 *
 * @param asyncResp Async response object
 * @param serviceName D-Bus service name
 * @param objectPath D-Bus object path
 */
inline void onArchiveServiceFoundForDownload(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& serviceName, const std::string& objectPath)
{
    dbus::utility::async_method_call(
        asyncResp,
        [asyncResp](const boost::system::error_code& methodEc,
                    sdbusplus::message::message& msg) {
            downloadArchiveCallback(asyncResp, methodEc, msg);
        },
        serviceName, objectPath, std::string(archiveInterface),
        std::string(archiveMethod));
}

inline void onArchiveServiceFoundForPost(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& /* serviceName */, const std::string& /* objectPath */)
{
    asyncResp->res.jsonValue["DownloadURI"] = boost::urls::format(
        "/redfish/v1/Systems/{}/LogServices/HostLogger/file",
        BMCWEB_REDFISH_SYSTEM_URI_NAME);
}

/**
 * @brief Process archive service discovery result
 *
 * @param asyncResp Async response object
 * @param callback Callback to invoke if service is found
 * @param ec Error code from D-Bus discovery operation
 * @param objInfo D-Bus object info containing service and interfaces
 */
template <typename Callback>
inline void afterArchiveServiceDiscovery(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp, Callback&& callback,
    const boost::system::error_code& ec,
    const dbus::utility::MapperGetObject& objInfo)
{
    if (ec || objInfo.empty())
    {
        BMCWEB_LOG_DEBUG("Archive service not found via object discovery");
        return;
    }

    const std::string& serviceName = objInfo.front().first;
    callback(asyncResp, serviceName, std::string(archiveObjectPath));
}

/**
 * @brief Helper function to discover archive service and invoke callback if
 * found
 *
 * @param asyncResp Async response object
 * @param callback Callback function to invoke if service is found
 */
template <typename Callback>
inline void discoverArchiveService(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp, Callback&& callback)
{
    constexpr std::array<std::string_view, 1> interfaces = {archiveInterface};

    dbus::utility::getDbusObject(
        std::string(archiveObjectPath), interfaces,
        [asyncResp, callback = std::forward<Callback>(callback)](
            const boost::system::error_code& ec,
            const dbus::utility::MapperGetObject& objInfo) mutable {
            afterArchiveServiceDiscovery(asyncResp, std::move(callback), ec,
                                         objInfo);
        });
}

inline void handleSystemsLogServicesHostloggerFileGet(
    App& app, const crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& systemName)
{
    if (!redfish::setUpRedfishRoute(app, req, asyncResp))
    {
        return;
    }

    // Clear response - this is a file download endpoint, not a resource
    // endpoint
    asyncResp->res.jsonValue.clear();

    if constexpr (BMCWEB_EXPERIMENTAL_REDFISH_MULTI_COMPUTER_SYSTEM)
    {
        messages::resourceNotFound(asyncResp->res, "ComputerSystem",
                                   systemName);
        return;
    }

    if (systemName != BMCWEB_REDFISH_SYSTEM_URI_NAME)
    {
        messages::resourceNotFound(asyncResp->res, "ComputerSystem",
                                   systemName);
        return;
    }

    discoverArchiveService(asyncResp,
                           std::bind_front(onArchiveServiceFoundForDownload));
}

inline void handleSystemsLogServicesHostloggerDownloadRawLogPost(
    App& app, const crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& systemName)
{
    if (!redfish::setUpRedfishRoute(app, req, asyncResp))
    {
        return;
    }

    if constexpr (BMCWEB_EXPERIMENTAL_REDFISH_MULTI_COMPUTER_SYSTEM)
    {
        messages::resourceNotFound(asyncResp->res, "ComputerSystem",
                                   systemName);
        return;
    }

    if (systemName != BMCWEB_REDFISH_SYSTEM_URI_NAME)
    {
        messages::resourceNotFound(asyncResp->res, "ComputerSystem",
                                   systemName);
        return;
    }

    discoverArchiveService(asyncResp,
                           std::bind_front(onArchiveServiceFoundForPost));
}

inline void requestRoutesSystemsLogServiceHostloggerDownload(App& app)
{
    BMCWEB_ROUTE(
        app,
        "/redfish/v1/Systems/<str>/LogServices/HostLogger/Actions/LogService.DownloadRawLog/")
        .privileges(redfish::privileges::postLogService)
        .methods(boost::beast::http::verb::post)(std::bind_front(
            handleSystemsLogServicesHostloggerDownloadRawLogPost,
            std::ref(app)));

    BMCWEB_ROUTE(app, "/redfish/v1/Systems/<str>/LogServices/HostLogger/file")
        .privileges(redfish::privileges::getLogService)
        .methods(boost::beast::http::verb::get)(std::bind_front(
            handleSystemsLogServicesHostloggerFileGet, std::ref(app)));
}

} // namespace redfish
