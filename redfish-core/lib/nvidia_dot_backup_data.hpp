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

#include "bmcweb_config.h"

#include "app.hpp"
#include "dbus_singleton.hpp"
#include "dbus_utility.hpp"
#include "error_messages.hpp"
#include "http/utility.hpp"
#include "logging.hpp"
#include "query.hpp"
#include "redfish_util.hpp"
#include "registries/privilege_registry.hpp"
#include "utils/collection.hpp"
#include "utils/json_utils.hpp"
#include "utils/memfd_utils.hpp"
#include "utils/nvidia_async_call_utils.hpp"

#include <unistd.h>

#include <boost/url/format.hpp>
#include <nlohmann/json.hpp>

#include <array>
#include <chrono>
#include <string>
#include <string_view>
#include <vector>

namespace redfish
{

constexpr const std::string_view dotBlobInterface = "com.nvidia.Dot.Blob";
constexpr const std::string_view dotBlobPathPrefix =
    "/xyz/openbmc_project/dot_blob/";
constexpr size_t maxDotBlobSize = 1024;
// Base64 length for exactly maxDotBlobSize bytes: ceiling formula ((n+2)/3)*4
constexpr size_t maxBase64Size = ((maxDotBlobSize + 2) / 3) * 4;
constexpr std::chrono::seconds dotBlobOperationTimeout{10};

/**
 * @brief Handle DOT backup data operation error result
 *
 * Generic error handler that processes DOT backup data async operation error
 * results, mapping operation status values to appropriate HTTP error responses.
 * Handles InvalidArgument, Unavailable, UnsupportedRequest, ResourceNotFound,
 * and generic error cases with appropriate error messages.
 *
 * @param asyncResp Async response object to populate with error result
 * @param status Operation status string from async operation
 * @param actionName Name of the DOT backup data action (e.g., "GetBlob",
 * "UpdateBlob")
 */
inline void handleDOTBackupDataErrorResult(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& status, const std::string& actionName)
{
    if (status == nvidia_async_operation_utils::asyncStatusValueInvalidArgument)
    {
        BMCWEB_LOG_ERROR("DOT Backup Data {} invalid argument", actionName);
        messages::actionParameterValueError(
            asyncResp->res, nlohmann::json(""),
            "NvidiaDOTBackupData." + actionName);
        return;
    }
    if (status == nvidia_async_operation_utils::asyncStatusValueUnavailable)
    {
        BMCWEB_LOG_ERROR("DOT Backup Data {} service unavailable", actionName);
        messages::serviceTemporarilyUnavailable(asyncResp->res, "60");
        return;
    }
    if (status ==
        nvidia_async_operation_utils::asyncStatusValueUnsupportedRequest)
    {
        BMCWEB_LOG_ERROR("DOT Backup Data {} unsupported by device",
                         actionName);
        messages::actionNotSupported(asyncResp->res,
                                     "NvidiaDOTBackupData." + actionName);
        return;
    }
    if (status ==
        nvidia_async_operation_utils::asyncStatusValueResourceNotFound)
    {
        BMCWEB_LOG_ERROR("DOT Backup Data {} resource not found", actionName);
        messages::resourceNotFound(asyncResp->res, "DOTBackupData", "");
        return;
    }
    if (status == nvidia_async_operation_utils::asyncStatusValueTimeout)
    {
        BMCWEB_LOG_ERROR("DOT Backup Data {} operation timed out", actionName);
        messages::operationTimeout(asyncResp->res);
        return;
    }
    if (status == nvidia_async_operation_utils::asyncStatusValueInternalFailure)
    {
        BMCWEB_LOG_ERROR("DOT Backup Data {} internal failure", actionName);
        messages::internalError(asyncResp->res);
        return;
    }
    BMCWEB_LOG_ERROR("DOT Backup Data {} failed with status: {}", actionName,
                     status);
    messages::internalError(asyncResp->res);
}

/**
 * @brief Callback after GetBlob D-Bus method call
 *
 * Reads data from the file descriptor returned by GetBlob, encodes it to
 * base64, and returns it in the response.
 *
 * @param asyncResp Async response object
 * @param ec Error code from D-Bus call
 * @param unixfd File descriptor containing the blob data
 */
inline void afterDOTBlobGetBlob(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const boost::system::error_code& ec,
    const sdbusplus::message::unix_fd& unixfd)
{
    if (ec)
    {
        BMCWEB_LOG_ERROR("D-Bus error calling GetBlob: {}", ec.message());
        messages::internalError(asyncResp->res);
        return;
    }

    int fd = dup(unixfd);
    if (fd < 0)
    {
        BMCWEB_LOG_ERROR("Failed to duplicate file descriptor");
        messages::internalError(asyncResp->res);
        return;
    }

    struct stat fileStat{};
    if (fstat(fd, &fileStat) < 0)
    {
        BMCWEB_LOG_ERROR("Failed to get file size");
        close(fd);
        messages::internalError(asyncResp->res);
        return;
    }

    if (static_cast<size_t>(fileStat.st_size) > maxBase64Size)
    {
        BMCWEB_LOG_ERROR(
            "DOT blob file size {} exceeds maximum allowed size {}",
            fileStat.st_size, maxBase64Size);
        close(fd);
        messages::internalError(asyncResp->res);
        return;
    }

    std::vector<uint8_t> data(static_cast<size_t>(fileStat.st_size));
    if (lseek(fd, 0, SEEK_SET) < 0)
    {
        BMCWEB_LOG_ERROR("Failed to seek to beginning");
        close(fd);
        messages::internalError(asyncResp->res);
        return;
    }

    ssize_t bytesRead = read(fd, data.data(), data.size());
    close(fd);

    if (bytesRead < 0 || static_cast<size_t>(bytesRead) != data.size())
    {
        BMCWEB_LOG_ERROR("Failed to read data from file descriptor");
        messages::internalError(asyncResp->res);
        return;
    }

    std::string dataStr(data.begin(), data.end());
    std::string base64Data = crow::utility::base64encode(dataStr);
    asyncResp->res.jsonValue["DOTData"] = base64Data;
    asyncResp->res.result(boost::beast::http::status::ok);
}

/**
 * @brief Common helper for DOT blob service discovery
 *
 * Processes the D-Bus discovery response to find the matching DOT blob service
 * and path for a given processor ID. Handles error cases and returns the
 * service and path if found.
 *
 * @param asyncResp Async response object for error reporting
 * @param processorId The processor identifier
 * @param actionName Name of the action for logging purposes
 * @param ec Error code from D-Bus discovery operation
 * @param resp D-Bus subtree response containing discovered DOT blob objects
 * @return Optional pair of (service, blobPath) if found, nullopt if error
 */
inline std::optional<std::pair<std::string, std::string>>
    findDOTBlobServiceAndPath(
        const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
        const std::string& processorId, const std::string& actionName,
        const boost::system::error_code& ec,
        const dbus::utility::MapperGetSubTreeResponse& resp)
{
    if (ec)
    {
        BMCWEB_LOG_ERROR("DOT Backup Data {}: GetSubTree error: {}", actionName,
                         ec.message());
        messages::internalError(asyncResp->res);
        return std::nullopt;
    }

    std::string blobPath = std::string(dotBlobPathPrefix) + processorId;
    std::string service;
    for (const auto& [path, serviceMap] : resp)
    {
        if (path == blobPath && !serviceMap.empty())
        {
            service = serviceMap[0].first;
            break;
        }
    }

    if (service.empty())
    {
        BMCWEB_LOG_ERROR(
            "DOT Backup Data {}: No service found for processor: {}",
            actionName, processorId);
        messages::resourceNotFound(asyncResp->res, "DOTBackupData",
                                   processorId);
        return std::nullopt;
    }
    BMCWEB_LOG_DEBUG("DOT Backup Data {}: Found service '{}' at path '{}'",
                     actionName, service, blobPath);

    return std::make_pair(service, blobPath);
}

/**
 * @brief Callback after discovering D-Bus service for DOT blob
 *
 * Calls GetBlob method to retrieve the backup data.
 *
 * @param asyncResp Async response object
 * @param processorId Processor identifier
 * @param ec Error code from D-Bus discovery
 * @param subtree D-Bus subtree response
 */
inline void afterDOTBlobServiceDiscovery(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& processorId, const boost::system::error_code& ec,
    const dbus::utility::MapperGetSubTreeResponse& subtree)
{
    auto servicePath = findDOTBlobServiceAndPath(asyncResp, processorId,
                                                 "GetBlob", ec, subtree);
    if (!servicePath.has_value())
    {
        return;
    }

    const auto& [service, blobPath] = *servicePath;
    dbus::utility::async_method_call(
        [asyncResp](const boost::system::error_code& ec2,
                    const sdbusplus::message::unix_fd& unixfd) {
            afterDOTBlobGetBlob(asyncResp, ec2, unixfd);
        },
        service, blobPath, std::string(dotBlobInterface), "GetBlob");
}

/**
 * @brief Callback after discovering D-Bus service for DOT blob upload
 *
 * Calls UpdateBlob method to upload the backup data.
 *
 * @param asyncResp Async response object
 * @param processorId Processor identifier
 * @param memFd Memory file descriptor containing the data
 * @param ec Error code from D-Bus discovery
 * @param subtree D-Bus subtree response
 */
inline void afterDOTBlobServiceDiscoveryForUpload(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& processorId, const std::shared_ptr<MemoryFD>& memFd,
    const boost::system::error_code& ec,
    const dbus::utility::MapperGetSubTreeResponse& subtree)
{
    auto servicePath = findDOTBlobServiceAndPath(asyncResp, processorId,
                                                 "UpdateBlob", ec, subtree);
    if (!servicePath.has_value())
    {
        return;
    }

    const auto& [service, blobPath] = *servicePath;
    nvidia_async_operation_utils::doGenericCallAsyncAndGatherResult<int>(
        asyncResp, dotBlobOperationTimeout, service, blobPath,
        std::string(dotBlobInterface), "UpdateBlob",
        [asyncResp](const std::string& status, const int* /*resultPtr*/) {
            if (status == nvidia_async_operation_utils::asyncStatusValueSuccess)
            {
                messages::success(asyncResp->res);
                return;
            }
            handleDOTBackupDataErrorResult(asyncResp, status, "UpdateBlob");
        },
        sdbusplus::message::unix_fd(memFd->fd));
}

/**
 * @brief Handler for GET requests to DOTBackupData collection
 *
 * Returns the collection of DOT backup data resources, with members
 * pointing to individual processor backup data resources.
 *
 * @param app Crow application instance
 * @param req HTTP request object
 * @param asyncResp Async response object
 * @param managerId Manager identifier from URL path
 */
inline void handleDOTBackupDataCollectionGet(
    crow::App& app, const crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& managerId)
{
    if (!redfish::setUpRedfishRoute(app, req, asyncResp))
    {
        return;
    }

    if (managerId != BMCWEB_REDFISH_MANAGER_URI_NAME)
    {
        messages::resourceNotFound(asyncResp->res, "Manager", managerId);
        return;
    }

    asyncResp->res.jsonValue["@odata.type"] =
        "#NvidiaDOTBackupDataCollection.NvidiaDOTBackupDataCollection";
    asyncResp->res.jsonValue["@odata.id"] = boost::urls::format(
        "/redfish/v1/Managers/{}/Oem/Nvidia/DOTBackupData", managerId);
    asyncResp->res.jsonValue["Name"] =
        std::format("{} Oem Nvidia DOTBackupData", managerId);

    constexpr std::array<std::string_view, 1> blobInterfaces = {
        dotBlobInterface};

    collection_util::getCollectionMembers(
        asyncResp,
        boost::urls::format("/redfish/v1/Managers/{}/Oem/Nvidia/DOTBackupData",
                            managerId),
        blobInterfaces, std::string(dotBlobPathPrefix));
}

/**
 * @brief Callback after processor discovery for RelatedItem
 *
 * Verifies processor exists and adds RelatedItem link if found.
 * Prevents broken links when the device/processor is down.
 *
 * @param asyncResp Async response object
 * @param processorId Processor identifier to verify
 * @param ec Error code from D-Bus discovery
 * @param subtree D-Bus subtree response containing discovered processors
 */
inline void afterProcessorDiscoveryForRelatedItem(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& processorId, const boost::system::error_code& ec,
    const dbus::utility::MapperGetSubTreeResponse& subtree)
{
    asyncResp->res.jsonValue["RelatedItem"] = nlohmann::json::array();
    if (!ec)
    {
        bool processorFound = false;
        for (const auto& [path, serviceMap] : subtree)
        {
            sdbusplus::message::object_path objPath(path);
            if (objPath.filename() == processorId)
            {
                processorFound = true;
                break;
            }
        }

        if (processorFound)
        {
            nlohmann::json::object_t relatedItem;
            relatedItem["@odata.id"] =
                std::format("/redfish/v1/Systems/{}/Processors/{}",
                            BMCWEB_REDFISH_SYSTEM_URI_NAME, processorId);
            asyncResp->res.jsonValue["RelatedItem"].emplace_back(
                std::move(relatedItem));
        }
    }
    asyncResp->res.jsonValue["RelatedItem@odata.count"] =
        asyncResp->res.jsonValue["RelatedItem"].size();
}

/**
 * @brief Callback after DOT blob discovery for DOTBackupData resource
 *
 * Validates DOT blob exists and populates the DOTBackupData resource
 * with properties, actions, and related items.
 *
 * @param asyncResp Async response object
 * @param managerId Manager identifier
 * @param processorId Processor identifier
 * @param ec Error code from D-Bus discovery
 * @param subtree D-Bus subtree response containing discovered DOT blobs
 */
inline void afterDOTBackupDataBlobDiscovery(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& managerId, const std::string& processorId,
    const boost::system::error_code& ec,
    const dbus::utility::MapperGetSubTreeResponse& subtree)
{
    if (ec)
    {
        BMCWEB_LOG_ERROR("D-Bus error getting DOT blobs: {}", ec.message());
        messages::internalError(asyncResp->res);
        return;
    }

    std::string blobPath = std::string(dotBlobPathPrefix) + processorId;
    bool blobFound = false;
    for (const auto& [path, serviceMap] : subtree)
    {
        if (path == blobPath)
        {
            blobFound = true;
            break;
        }
    }

    if (!blobFound)
    {
        messages::resourceNotFound(asyncResp->res, "DOTBackupData",
                                   processorId);
        return;
    }

    asyncResp->res.jsonValue["@odata.type"] =
        "#NvidiaDOTBackupData.v1_0_0.NvidiaDOTBackupData";
    asyncResp->res.jsonValue["@odata.id"] = boost::urls::format(
        "/redfish/v1/Managers/{}/Oem/Nvidia/DOTBackupData/{}", managerId,
        processorId);
    asyncResp->res.jsonValue["Id"] = processorId;
    asyncResp->res.jsonValue["Name"] =
        std::format("{} Oem Nvidia DOTBackupData {}", managerId, processorId);

    asyncResp->res.jsonValue["Actions"]["#NvidiaDOTBackupData.Upload"]
                            ["@Redfish.ActionInfo"] = boost::urls::format(
        "/redfish/v1/Managers/{}/Oem/Nvidia/DOTBackupData/{}/UploadActionInfo",
        managerId, processorId);
    asyncResp->res.jsonValue["Actions"]["#NvidiaDOTBackupData.Upload"]
                            ["target"] = boost::urls::format(
        "/redfish/v1/Managers/{}/Oem/Nvidia/DOTBackupData/{}/Actions/NvidiaDOTBackupData.Upload",
        managerId, processorId);

    asyncResp->res.jsonValue["Actions"]["#NvidiaDOTBackupData.Download"]
                            ["target"] = boost::urls::format(
        "/redfish/v1/Managers/{}/Oem/Nvidia/DOTBackupData/{}/Actions/NvidiaDOTBackupData.Download",
        managerId, processorId);

    constexpr std::array<std::string_view, 1> processorInterfaces = {
        "xyz.openbmc_project.Inventory.Item.Cpu"};

    dbus::utility::getSubTree(
        "/xyz/openbmc_project/inventory", 0, processorInterfaces,
        std::bind_front(afterProcessorDiscoveryForRelatedItem, asyncResp,
                        processorId));
}

/**
 * @brief Handler for GET requests to individual DOTBackupData resource
 *
 * Returns the individual DOT backup data resource for a specific processor,
 * including actions and related item links.
 *
 * @param app Crow application instance
 * @param req HTTP request object
 * @param asyncResp Async response object
 * @param managerId Manager identifier from URL path
 * @param processorId Processor identifier from URL path
 */
inline void handleDOTBackupDataGet(
    crow::App& app, const crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& managerId, const std::string& processorId)
{
    if (!redfish::setUpRedfishRoute(app, req, asyncResp))
    {
        return;
    }

    if (managerId != BMCWEB_REDFISH_MANAGER_URI_NAME)
    {
        messages::resourceNotFound(asyncResp->res, "Manager", managerId);
        return;
    }

    constexpr std::array<std::string_view, 1> blobInterfaces = {
        dotBlobInterface};

    dbus::utility::getSubTree(
        std::string(dotBlobPathPrefix), 0, blobInterfaces,
        std::bind_front(afterDOTBackupDataBlobDiscovery, asyncResp, managerId,
                        processorId));
}

/**
 * @brief Handler for UploadActionInfo GET requests
 *
 * Returns the ActionInfo resource describing the Upload action parameters.
 *
 * @param app Crow application instance
 * @param req HTTP request object
 * @param asyncResp Async response object
 * @param managerId Manager identifier from URL path
 * @param processorId Processor identifier from URL path
 */
inline void handleDOTBackupDataUploadActionInfo(
    crow::App& app, const crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& managerId, const std::string& processorId)
{
    if (!redfish::setUpRedfishRoute(app, req, asyncResp))
    {
        return;
    }

    if (managerId != BMCWEB_REDFISH_MANAGER_URI_NAME)
    {
        messages::resourceNotFound(asyncResp->res, "Manager", managerId);
        return;
    }

    asyncResp->res.jsonValue["@odata.id"] = boost::urls::format(
        "/redfish/v1/Managers/{}/Oem/Nvidia/DOTBackupData/{}/UploadActionInfo",
        managerId, processorId);
    asyncResp->res.jsonValue["@odata.type"] = "#ActionInfo.v1_2_0.ActionInfo";
    asyncResp->res.jsonValue["Id"] = "UploadActionInfo";
    asyncResp->res.jsonValue["Name"] =
        std::format("{} Oem Nvidia DOTBackupData {} UploadActionInfo",
                    managerId, processorId);

    nlohmann::json::array_t parameters;
    nlohmann::json::object_t parameter;
    parameter["Name"] = "DOTData";
    parameter["Required"] = true;
    parameter["DataType"] = "String";
    parameters.emplace_back(std::move(parameter));
    asyncResp->res.jsonValue["Parameters"] = std::move(parameters);
}

/**
 * @brief Handler for Upload action POST requests
 *
 * Handles the upload of DOT backup data for a processor.
 *
 * @param app Crow application instance
 * @param req HTTP request object containing DOTData parameter
 * @param asyncResp Async response object
 * @param managerId Manager identifier from URL path
 * @param processorId Processor identifier from URL path
 */
inline void handleDOTBackupDataUploadAction(
    crow::App& app, const crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& managerId, const std::string& processorId)
{
    if (!redfish::setUpRedfishRoute(app, req, asyncResp))
    {
        return;
    }

    if (managerId != BMCWEB_REDFISH_MANAGER_URI_NAME)
    {
        messages::resourceNotFound(asyncResp->res, "Manager", managerId);
        return;
    }

    std::string dotData;
    if (!json_util::readJsonAction(req, asyncResp->res, "DOTData", dotData))
    {
        BMCWEB_LOG_ERROR("DOT Backup Data Upload: Missing DOTData parameter");
        return;
    }

    if (dotData.empty())
    {
        messages::actionParameterValueError(asyncResp->res, "DOTData",
                                            "NvidiaDOTBackupData.Upload");
        return;
    }

    if (dotData.size() > maxBase64Size)
    {
        BMCWEB_LOG_ERROR(
            "DOT Backup Data Upload: DOTData size {} exceeds maximum allowed size {}",
            dotData.size(), maxBase64Size);
        messages::actionParameterValueFormatError(
            asyncResp->res, dotData, "DOTData", "NvidiaDOTBackupData.Upload");
        return;
    }

    std::string binaryData;
    if (!crow::utility::base64Decode(dotData, binaryData))
    {
        messages::actionParameterValueFormatError(
            asyncResp->res, dotData, "DOTData", "NvidiaDOTBackupData.Upload");
        return;
    }

    if (binaryData.size() != maxDotBlobSize)
    {
        BMCWEB_LOG_ERROR(
            "DOT Backup Data Upload: DOTData decoded size {} is invalid; expected exactly {} bytes",
            binaryData.size(), maxDotBlobSize);
        messages::actionParameterValueError(asyncResp->res, "DOTData",
                                            "NvidiaDOTBackupData.Upload");
        return;
    }

    std::shared_ptr<MemoryFD> memFd;
    try
    {
        memFd = std::make_shared<MemoryFD>();
        std::vector<uint8_t> data(binaryData.begin(), binaryData.end());
        memFd->write(data);
    }
    catch (const std::exception& e)
    {
        BMCWEB_LOG_ERROR("Failed to create memory file descriptor: {}",
                         e.what());
        messages::internalError(asyncResp->res);
        return;
    }

    constexpr std::array<std::string_view, 1> blobInterfaces = {
        dotBlobInterface};

    dbus::utility::getSubTree(
        std::string(dotBlobPathPrefix), 0, blobInterfaces,
        std::bind_front(afterDOTBlobServiceDiscoveryForUpload, asyncResp,
                        processorId, memFd));
}

/**
 * @brief Handler for Download action POST requests
 *
 * Handles the download of DOT backup data for a processor.
 *
 * @param app Crow application instance
 * @param req HTTP request object
 * @param asyncResp Async response object
 * @param managerId Manager identifier from URL path
 * @param processorId Processor identifier from URL path
 */
inline void handleDOTBackupDataDownloadAction(
    crow::App& app, const crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& managerId, const std::string& processorId)
{
    if (!redfish::setUpRedfishRoute(app, req, asyncResp))
    {
        return;
    }

    if (managerId != BMCWEB_REDFISH_MANAGER_URI_NAME)
    {
        messages::resourceNotFound(asyncResp->res, "Manager", managerId);
        return;
    }

    constexpr std::array<std::string_view, 1> blobInterfaces = {
        dotBlobInterface};

    dbus::utility::getSubTree(
        std::string(dotBlobPathPrefix), 0, blobInterfaces,
        std::bind_front(afterDOTBlobServiceDiscovery, asyncResp, processorId));
}

inline void requestRoutesDOTBackupDataCollection(crow::App& app)
{
    BMCWEB_ROUTE(app, "/redfish/v1/Managers/<str>/Oem/Nvidia/DOTBackupData/")
        .privileges(redfish::privileges::getManager)
        .methods(boost::beast::http::verb::get)(
            std::bind_front(handleDOTBackupDataCollectionGet, std::ref(app)));

    BMCWEB_ROUTE(app,
                 "/redfish/v1/Managers/<str>/Oem/Nvidia/DOTBackupData/<str>/")
        .privileges(redfish::privileges::getManager)
        .methods(boost::beast::http::verb::get)(
            std::bind_front(handleDOTBackupDataGet, std::ref(app)));

    BMCWEB_ROUTE(
        app,
        "/redfish/v1/Managers/<str>/Oem/Nvidia/DOTBackupData/<str>/UploadActionInfo")
        .privileges(redfish::privileges::getActionInfo)
        .methods(boost::beast::http::verb::get)(std::bind_front(
            handleDOTBackupDataUploadActionInfo, std::ref(app)));

    BMCWEB_ROUTE(
        app,
        "/redfish/v1/Managers/<str>/Oem/Nvidia/DOTBackupData/<str>/Actions/NvidiaDOTBackupData.Upload")
        .privileges(redfish::privileges::postManager)
        .methods(boost::beast::http::verb::post)(
            std::bind_front(handleDOTBackupDataUploadAction, std::ref(app)));

    BMCWEB_ROUTE(
        app,
        "/redfish/v1/Managers/<str>/Oem/Nvidia/DOTBackupData/<str>/Actions/NvidiaDOTBackupData.Download")
        .privileges(redfish::privileges::postManager)
        .methods(boost::beast::http::verb::post)(
            std::bind_front(handleDOTBackupDataDownloadAction, std::ref(app)));
}

} // namespace redfish
