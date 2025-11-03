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

#include "query.hpp"
#include "registries/privilege_registry.hpp"

#include <app.hpp>
#include <utils/memfd_utils.hpp>
#include <utils/privilege_utils.hpp>

namespace redfish
{

namespace smbios
{

constexpr uint32_t smbiosTableStorageSize = 64 * 1024;

/**
 * @brief Handles the SMBIOS blob upload operation for the system
 *
 * This function processes a request to update the SMBIOS blob in the BMC. It:
 * 1. Validates the size of the incoming SMBIOS blob data (max 64KB)
 * 2. Calls the Upload method of the xyz.openbmc_project.Smbios.Blob service
 *    to upload the SMBIOS blob to the BMC
 *
 * @param req The HTTP request containing the SMBIOS blob data
 * @param asyncResp The asynchronous response object to send back to the client
 * @return void
 */
inline void pushSmbiosTable(const crow::Request& req,
                            const std::shared_ptr<bmcweb::AsyncResp>& asyncResp)
{
    BMCWEB_LOG_DEBUG("Push SMBIOS Table");
    // SMBIOS blob is processed later in the BMC by the smbios-mdrv2 service
    // which will reject any blob that is larger than 'smbiosTableStorageSize'
    // bytes.
    BMCWEB_LOG_DEBUG("SMBIOS blob size is {} bytes", req.body().size());
    if (req.body().size() > smbiosTableStorageSize)
    {
        BMCWEB_LOG_ERROR("SMBIOS blob size ({} bytes) is too large",
                         req.body().size());
        messages::payloadTooLarge(asyncResp->res);
        return;
    }

    BMCWEB_LOG_DEBUG("Copying request body to memory file descriptor...");
    std::shared_ptr<MemoryFD> blobFd;
    try
    {
        blobFd = std::make_shared<MemoryFD>();
        std::vector<uint8_t> blobData(req.body().begin(), req.body().end());
        blobFd->write(blobData);
    }
    catch (const std::exception& err)
    {
        BMCWEB_LOG_ERROR("Failed to write SMBIOS data to memory: {}",
                         err.what());
        messages::internalError(asyncResp->res);
        return;
    }

    BMCWEB_LOG_DEBUG("Uploading SMBIOS blob...");

    dbus::utility::async_method_call(
        // In the lambda capture, we move the blobFd to transfer ownership to
        // the callback in order to keep the shared pointer alive until the dbus
        // method call completes and returns
        [asyncResp, blobFd](const boost::system::error_code ec, bool status) {
            if (ec)
            {
                BMCWEB_LOG_ERROR("Failed to call Upload: {}", ec.message());
                messages::internalError(asyncResp->res);
                return;
            }
            if (!status)
            {
                BMCWEB_LOG_ERROR("SMBIOS data synchronization failed");
                messages::internalError(asyncResp->res);
                return;
            }
            BMCWEB_LOG_DEBUG("SMBIOS data synchronization successful");
            messages::success(asyncResp->res);
        },
        "xyz.openbmc_project.Smbios.BlobUpdater",
        "/xyz/openbmc_project/Smbios/BlobUpdater",
        "xyz.openbmc_project.Smbios.Blob", "Upload",
        sdbusplus::message::unix_fd(blobFd->fd));
}

/**
 * @brief Validates privileges and system name before processing SMBIOS blob
 * update
 *
 * This function performs the necessary privilege and system validation checks
 * before allowing the SMBIOS blob upload operation. It:
 * 1. Validates the Redfish route setup
 * 2. Verifies the system name matches the expected BMC system URI
 * 3. Checks if the requesting user has BIOS privileges
 * 4. Only proceeds with the SMBIOS blob upload if all checks pass
 *
 * @param app The Crow application instance
 * @param req The HTTP request containing the SMBIOS blob data
 * @param asyncResp The asynchronous response object to send back to the client
 * @param systemName The name of the system being targeted for the update
 * @return void
 */
inline void pushSmbiosTablePrivilegeCheck(
    crow::App& app, const crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& systemName)
{
    BMCWEB_LOG_DEBUG("Push SMBIOS Blob Privilege Check");

    if (!redfish::setUpRedfishRoute(app, req, asyncResp))
    {
        BMCWEB_LOG_ERROR("Failed to set up Redfish route");
        return;
    }
    if (systemName != BMCWEB_REDFISH_SYSTEM_URI_NAME)
    {
        messages::resourceNotFound(asyncResp->res, "ComputerSystem",
                                   systemName);
        return;
    }

    if (req.session == nullptr)
    {
        BMCWEB_LOG_ERROR("Session is null");
        messages::insufficientPrivilege(asyncResp->res);
        return;
    }
    privilege_utils::isBiosPrivilege(
        req.session->username,
        [&req, asyncResp](const boost::system::error_code& ec, bool isBios) {
            if (ec)
            {
                BMCWEB_LOG_ERROR("Failed to check BIOS privilege: {}",
                                 ec.message());
                messages::internalError(asyncResp->res);
                return;
            }
            if (!isBios)
            {
                BMCWEB_LOG_DEBUG("Action requires BIOS privilege");
                messages::insufficientPrivilege(asyncResp->res);
                return;
            }
            pushSmbiosTable(req, asyncResp);
        });
}

} // namespace smbios

inline void requestRoutesNvidiaSmbios(App& app)
{
    if constexpr (BMCWEB_PUSH_SMBIOS_TABLE_FEATURE)
    {
        BMCWEB_ROUTE(
            app,
            "/redfish/v1/Systems/<str>/Actions/Oem/NvidiaComputerSystem.PushSmbiosTable")
            .privileges(redfish::privileges::postComputerSystem)
            .methods(boost::beast::http::verb::post)(std::bind_front(
                smbios::pushSmbiosTablePrivilegeCheck, std::ref(app)));
    }
}

} // namespace redfish
