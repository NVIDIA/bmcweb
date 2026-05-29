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

#pragma once

/**
 * @file nvidia_fabric_config_update.hpp
 *
 * Implements the Oem.Nvidia.SwitchConfigPushURI endpoint for Fabric resources.
 *
 * Exposed routes (registered by requestRoutesNvidiaConfigFile)
 * ------------------------------------------------------------
 *   POST   /redfish/v1/Fabrics/<fabricId>/upload-switch-config
 *     Upload a binary switch configuration file.  The request body MUST be
 *     multipart/form-data with exactly one part:
 *       name      = "ImportFile"
 *       filename  = <name as provided by NVIDIA>
 *       content   = raw binary config data
 *     No other form fields are permitted.
 *     On success the file is forwarded as a unix_fd (D-Bus type 'h') to
 *     AddConfigFile() on com.nvidia.SwitchConfig.Updater.
 *     Returns HTTP 204 on success.
 *
 *   DELETE /redfish/v1/Fabrics/<fabricId>/upload-switch-config
 *     Calls RemoveConfigFile() on com.nvidia.SwitchConfig.Updater.
 *     Returns HTTP 204 on success, 404 when no file is present.
 *
 * D-Bus interface
 * ---------------
 *   Interface : com.nvidia.SwitchConfig.Updater
 *   Object    : fabric's D-Bus inventory path
 *   Methods   :
 *     AddConfigFile(h file_fd) → void
 *       h — unix file descriptor with config file content.
 *       Throws: FileAlreadyExists, FileEmpty, FileTooLarge, InvalidStructure.
 *     RemoveConfigFile() → void
 *       Throws when no file is present.
 *
 * OEM property (Fabric GET)
 * -------------------------
 *   addSwitchConfigPushURI() performs a D-Bus lookup for
 *   com.nvidia.SwitchConfig.Updater before advertising the URI.
 *   Called from requestRoutesFabric() in redfish-core/lib/fabric.hpp.
 */

#include "app.hpp"
#include "async_resp.hpp"
#include "dbus_utility.hpp"
#include "error_messages.hpp"
#include "logging.hpp"
#include "multipart_parser.hpp"
#include "nvidia_error_messages.hpp"
#include "query.hpp"
#include "registries/privilege_registry.hpp"

#include <sys/mman.h>
#include <unistd.h>

#include <boost/beast/http/verb.hpp>
#include <boost/system/error_code.hpp>

#include <array>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

namespace redfish
{

static constexpr std::string_view kSwitchCfgUpdaterIface =
    "com.nvidia.SwitchConfig.Updater";

static constexpr std::string_view kFabricIface =
    "xyz.openbmc_project.Inventory.Item.Fabric";

// Association name attached to each Fabric object that points to its
// com.nvidia.SwitchConfig.Updater object.
static constexpr std::string_view kSwitchCfgAssociation =
    "switch_config_updater";

// Maximum accepted request body for a config file upload.
// The D-Bus service enforces a 64 KB file limit; 256 KB gives reasonable
// headroom for multipart framing while catching obviously oversized payloads
// before parsing.
static constexpr size_t switchConfigBodyLimit = 256UL * 1024;

// ---------------------------------------------------------------------------
// RAII memfd wrapper
// ---------------------------------------------------------------------------

/**
 * @brief RAII wrapper for the anonymous memfd used to pass config file content
 *        to AddConfigFile(h).
 *
 *        Held as a shared_ptr so the fd stays alive across the async D-Bus
 *        chain until the kernel has duplicated it on the receiving side.
 */
struct SwitchCfgMemFd
{
    int fd = -1;

    SwitchCfgMemFd() : fd(memfd_create("switch-config", 0)) {}

    SwitchCfgMemFd(const SwitchCfgMemFd&) = delete;
    SwitchCfgMemFd& operator=(const SwitchCfgMemFd&) = delete;
    SwitchCfgMemFd(SwitchCfgMemFd&&) = delete;
    SwitchCfgMemFd& operator=(SwitchCfgMemFd&&) = delete;

    ~SwitchCfgMemFd()
    {
        if (fd != -1)
        {
            close(fd);
        }
    }

    bool rewind() const
    {
        return lseek(fd, 0, SEEK_SET) != static_cast<off_t>(-1);
    }
};

// ---------------------------------------------------------------------------
// OEM property helper — called from requestRoutesFabric GET handler
// ---------------------------------------------------------------------------

/**
 * @brief Follow the "switch_config_updater" association on @p fabricObjPath
 *        and, if the association exists, inject the SwitchConfigPushURI OEM
 *        property into the in-flight Fabric GET response.
 *
 * Silently skips the property when the association is absent.
 */
inline void switchCfgInjectPushURI(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& fabricId, const std::string& fabricObjPath)
{
    std::string assocPath =
        fabricObjPath + "/" + std::string(kSwitchCfgAssociation);
    dbus::utility::getAssociationEndPoints(
        assocPath, [asyncResp, fabricId,
                    fabricObjPath](const boost::system::error_code& ec,
                                   const dbus::utility::MapperEndPoints& eps) {
            if (ec || eps.empty())
            {
                BMCWEB_LOG_DEBUG(
                    "switch-config [switchCfgInjectPushURI]: association "
                    "'{}' absent on '{}'; skipping SwitchConfigPushURI "
                    "for fabric {}",
                    kSwitchCfgAssociation, fabricObjPath, fabricId);
                return;
            }
            asyncResp->res.jsonValue["Oem"]["Nvidia"]["@odata.type"] =
                "#NvidiaFabric.v1_0_0.NvidiaFabric";
            asyncResp->res.jsonValue["Oem"]["Nvidia"]["SwitchConfigPushURI"] =
                "/redfish/v1/Fabrics/" + fabricId + "/upload-switch-config";
        });
}

/**
 * @brief Inject Oem.Nvidia.SwitchConfigPushURI into a Fabric GET response.
 *
 * Advertises the URI only when the fabric object in D-Bus inventory has a
 * "switch_config_updater" association pointing to a
 * com.nvidia.SwitchConfig.Updater object.  Silently skips the property when
 * the association is absent (fabric does not support config push).
 *
 * Called from the Fabric GET D-Bus callback in fabric.hpp after standard
 * properties have been set:
 * @code
 *   redfish::addSwitchConfigPushURI(asyncResp, fabricId);
 * @endcode
 */
inline void addSwitchConfigPushURI(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& fabricId)
{
    dbus::utility::getSubTree(
        "/xyz/openbmc_project/inventory", 0,
        std::array<std::string_view, 1>{kFabricIface},
        [asyncResp,
         fabricId](const boost::system::error_code& ec,
                   const dbus::utility::MapperGetSubTreeResponse& subtree) {
            if (ec)
            {
                BMCWEB_LOG_ERROR(
                    "switch-config [addSwitchConfigPushURI]: GetSubTree "
                    "error for {}: {}",
                    kFabricIface, ec.message());
                return;
            }

            std::string fabricObjPath;
            for (const auto& [path, connNames] : subtree)
            {
                auto pos = path.rfind('/');
                std::string_view leaf =
                    (pos != std::string::npos)
                        ? std::string_view(path).substr(pos + 1)
                        : std::string_view(path);
                if (leaf == fabricId)
                {
                    fabricObjPath = path;
                    break;
                }
            }
            if (fabricObjPath.empty())
            {
                BMCWEB_LOG_DEBUG(
                    "switch-config [addSwitchConfigPushURI]: fabric '{}' "
                    "not found in inventory; skipping SwitchConfigPushURI",
                    fabricId);
                return;
            }

            switchCfgInjectPushURI(asyncResp, fabricId, fabricObjPath);
        });
}

// ---------------------------------------------------------------------------
// D-Bus error mapper
// ---------------------------------------------------------------------------

/**
 * @brief Map a D-Bus error from AddConfigFile to an HTTP response.
 *
 * Mirrors the handleUpdateErrorType pattern in update_service.hpp: each
 * known error name maps to the most appropriate Redfish message.
 *
 * @param asyncResp  Shared async response.
 * @param uploadUri  The request URI (used in Redfish error bodies).
 * @param ec         D-Bus error; ec.message() carries the error name string.
 */
inline void handleAddConfigFileError(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& uploadUri, const boost::system::error_code& ec)
{
    const std::string& errMsg = ec.message();

    if (errMsg.find("FileAlreadyExists") != std::string::npos ||
        errMsg.find("AlreadyExists") != std::string::npos)
    {
        BMCWEB_LOG_ERROR(
            "switch-config [handleAddConfigFileError]: matched AlreadyExists "
            "(D-Bus error: '{}')",
            errMsg);
        // 409 Conflict — base message tells what already exists.
        messages::resourceAlreadyExists(asyncResp->res, "SwitchConfig",
                                        "ConfigFile", uploadUri);
        // Second entry in @Message.ExtendedInfo: actionable resolution.
        messages::addMessageToErrorJson(
            asyncResp->res.jsonValue,
            messages::invalidUpload(
                uploadUri,
                "A switch configuration file is already present for this "
                "fabric. Send a DELETE request to " +
                    uploadUri +
                    " to remove the existing file before uploading a new "
                    "one."));
    }
    else if (errMsg.find("FileEmpty") != std::string::npos)
    {
        BMCWEB_LOG_ERROR(
            "switch-config [handleAddConfigFileError]: matched FileEmpty");
        messages::invalidUpload(asyncResp->res, uploadUri,
                                "Uploaded switch configuration file is empty");
    }
    else if (errMsg.find("FileTooLarge") != std::string::npos)
    {
        BMCWEB_LOG_ERROR(
            "switch-config [handleAddConfigFileError]: matched FileTooLarge");
        messages::payloadTooLarge(asyncResp->res);
    }
    else if (errMsg.find("InvalidStructure") != std::string::npos ||
             errMsg.find("ValidationFailed") != std::string::npos)
    {
        BMCWEB_LOG_ERROR("switch-config [handleAddConfigFileError]: matched "
                         "InvalidStructure/ValidationFailed");
        messages::invalidUpload(
            asyncResp->res, uploadUri,
            "Switch configuration file failed structural validation");
    }
    else
    {
        BMCWEB_LOG_ERROR(
            "switch-config [handleAddConfigFileError]: unhandled D-Bus "
            "error: '{}' — add a matching branch if this recurs",
            errMsg);
        messages::internalError(asyncResp->res);
    }
}

// ---------------------------------------------------------------------------
// Multipart parser helpers
// ---------------------------------------------------------------------------

/**
 * @brief Scan @p mimeParts for the single required "ImportFile" part.
 *
 * Enforces:
 *   - Every part carries a Content-Disposition header.
 *   - Exactly one part is named "ImportFile" with a non-empty filename.
 *   - No other part names are permitted.
 *   - The ImportFile content is non-empty.
 *
 * On validation failure a suitable Redfish error is written into @p asyncResp
 * and std::nullopt is returned.  On success the raw file bytes are returned.
 */
inline std::optional<std::string> scanMimePartsForImportFile(
    const std::span<const FormPart> mimeParts,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& fabricId, const std::string& uploadUri)
{
    std::string importFileContent;
    bool foundImportFile = false;

    for (const FormPart& part : mimeParts)
    {
        auto cdIt = part.fields.find("Content-Disposition");
        if (cdIt == part.fields.end())
        {
            BMCWEB_LOG_ERROR(
                "switch-config POST [scanMimePartsForImportFile]: form part "
                "missing Content-Disposition for fabric {}",
                fabricId);
            messages::invalidUpload(
                asyncResp->res, uploadUri,
                "Form part is missing Content-Disposition header");
            return std::nullopt;
        }

        std::string_view cdVal = cdIt->value();
        size_t semi = cdVal.find(';');
        if (semi == std::string_view::npos)
        {
            BMCWEB_LOG_ERROR(
                "switch-config POST [scanMimePartsForImportFile]: malformed "
                "Content-Disposition (no semicolon) for fabric {}",
                fabricId);
            messages::invalidUpload(asyncResp->res, uploadUri,
                                    "Malformed Content-Disposition header");
            return std::nullopt;
        }

        std::optional<std::string> fieldName;
        bool hasFilename = false;
        for (const auto& param :
             boost::beast::http::param_list{cdVal.substr(semi)})
        {
            if (param.first == "name" && !param.second.empty())
            {
                fieldName = std::string(param.second);
            }
            if (param.first == "filename" && !param.second.empty())
            {
                hasFilename = true;
                BMCWEB_LOG_DEBUG(
                    "switch-config POST [scanMimePartsForImportFile]: "
                    "filename='{}' for fabric {}",
                    param.second, fabricId);
            }
        }

        if (!fieldName)
        {
            BMCWEB_LOG_ERROR(
                "switch-config POST [scanMimePartsForImportFile]: cannot "
                "parse field name for fabric {}",
                fabricId);
            messages::invalidUpload(asyncResp->res, uploadUri,
                                    "Unable to determine form field name");
            return std::nullopt;
        }

        // Spec: "No parameters are allowed for this operation."
        if (*fieldName != "ImportFile")
        {
            BMCWEB_LOG_ERROR(
                "switch-config POST [scanMimePartsForImportFile]: unexpected "
                "field '{}' for fabric {}",
                *fieldName, fabricId);
            asyncResp->res.result(boost::beast::http::status::bad_request);
            messages::invalidUpload(
                asyncResp->res, uploadUri,
                "Unexpected form field '" + *fieldName +
                    "'; only 'ImportFile' is allowed — no parameters permitted");
            return std::nullopt;
        }

        if (foundImportFile)
        {
            BMCWEB_LOG_ERROR(
                "switch-config POST [scanMimePartsForImportFile]: duplicate "
                "ImportFile part for fabric {}",
                fabricId);
            asyncResp->res.result(boost::beast::http::status::bad_request);
            messages::invalidUpload(asyncResp->res, uploadUri,
                                    "Duplicate 'ImportFile' part");
            return std::nullopt;
        }

        // Spec: "the filename field shall reflect the name of the file as
        //        provided by NVIDIA and loaded by the client."
        if (!hasFilename)
        {
            BMCWEB_LOG_ERROR(
                "switch-config POST [scanMimePartsForImportFile]: ImportFile "
                "missing filename for fabric {}",
                fabricId);
            asyncResp->res.result(boost::beast::http::status::bad_request);
            messages::invalidUpload(
                asyncResp->res, uploadUri,
                "ImportFile must include a 'filename' field in "
                "Content-Disposition reflecting the file name as provided by "
                "NVIDIA");
            return std::nullopt;
        }

        importFileContent = part.content;
        foundImportFile = true;
    }

    if (!foundImportFile)
    {
        BMCWEB_LOG_ERROR(
            "switch-config POST [scanMimePartsForImportFile]: no ImportFile "
            "part in request for fabric {}",
            fabricId);
        asyncResp->res.result(boost::beast::http::status::bad_request);
        messages::invalidUpload(
            asyncResp->res, uploadUri,
            "Request must contain a form part named 'ImportFile'");
        return std::nullopt;
    }

    if (importFileContent.empty())
    {
        BMCWEB_LOG_ERROR(
            "switch-config POST [scanMimePartsForImportFile]: ImportFile "
            "content is empty for fabric {}",
            fabricId);
        messages::invalidUpload(asyncResp->res, uploadUri,
                                "Uploaded switch configuration file is empty");
        return std::nullopt;
    }

    return importFileContent;
}

// ---------------------------------------------------------------------------
// Shared D-Bus resolution helpers (POST + DELETE)
// ---------------------------------------------------------------------------

// Callback type used by switchCfgResolveUpdater callers.
// Called with (service, updaterObjPath) on successful resolution.
using SwitchCfgCallback =
    std::function<void(const std::string&, const std::string&)>;

/**
 * @brief Handle getDbusObject reply for the updater object.
 *
 * Validates the object map and invokes @p callback(service, updaterObjPath).
 * Extracted from switchCfgResolveUpdater to avoid nested lambdas.
 */
inline void onDbusObjectReply(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& fabricId, const std::string& updaterObjPath,
    const std::string& context, const SwitchCfgCallback& callback,
    const boost::system::error_code& ec,
    const dbus::utility::MapperGetObject& obj)
{
    if (ec || obj.empty())
    {
        BMCWEB_LOG_ERROR(
            "switch-config {} [onDbusObjectReply]: {} not found on '{}' "
            "for fabric {}: {}",
            context, kSwitchCfgUpdaterIface, updaterObjPath, fabricId,
            ec ? ec.message() : "empty object map");
        messages::resourceNotFound(asyncResp->res, "SwitchConfig.Updater",
                                   fabricId);
        return;
    }

    const std::string& service = obj.begin()->first;
    callback(service, updaterObjPath);
}

/**
 * @brief Handle getAssociationEndPoints reply for the switch_config_updater
 *        association.
 *
 * Validates the endpoint list, then calls getDbusObject on the updater path.
 * Extracted from switchCfgResolveUpdater to avoid nested lambdas.
 */
inline void onAssocEndPointsReply(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& fabricId, const std::string& fabricObjPath,
    const std::string& context, SwitchCfgCallback callback,
    const boost::system::error_code& ec,
    const dbus::utility::MapperEndPoints& eps)
{
    if (ec || eps.empty())
    {
        BMCWEB_LOG_ERROR(
            "switch-config {} [onAssocEndPointsReply]: association '{}' "
            "absent on '{}' for fabric {}: {}",
            context, kSwitchCfgAssociation, fabricObjPath, fabricId,
            ec ? ec.message() : "empty endpoints");
        messages::resourceNotFound(asyncResp->res, "SwitchConfig.Updater",
                                   fabricId);
        return;
    }

    const std::string& updaterObjPath = eps.front();
    dbus::utility::getDbusObject(
        updaterObjPath, std::array<std::string_view, 1>{kSwitchCfgUpdaterIface},
        std::bind_front(onDbusObjectReply, asyncResp, fabricId, updaterObjPath,
                        context, std::move(callback)));
}

/**
 * @brief Follow the "switch_config_updater" association from @p fabricObjPath,
 *        resolve the service name for the resulting updater object, then invoke
 *        @p callback(service, updaterObjPath).
 *
 * On any failure a suitable Redfish error is written into @p asyncResp and
 * @p callback is not called.
 */
inline void switchCfgResolveUpdater(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& fabricId, const std::string& fabricObjPath,
    const std::string& context, SwitchCfgCallback callback)
{
    std::string assocPath =
        fabricObjPath + "/" + std::string(kSwitchCfgAssociation);
    dbus::utility::getAssociationEndPoints(
        assocPath,
        std::bind_front(onAssocEndPointsReply, asyncResp, fabricId,
                        fabricObjPath, context, std::move(callback)));
}

// ---------------------------------------------------------------------------
// POST async chain — upload binary switch configuration
// ---------------------------------------------------------------------------

/**
 * @brief Handle the AddConfigFile D-Bus reply.
 *
 * Maps D-Bus errors to Redfish messages via handleAddConfigFileError.
 * The @p memfd parameter has no use in the body; it is held here solely to
 * keep the shared_ptr (and therefore the file descriptor) alive until the
 * kernel has duplicated the fd on the receiving D-Bus service side.
 */
inline void onAddConfigFileReply(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& fabricId, const std::string& uploadUri,
    const std::shared_ptr<SwitchCfgMemFd>& /*memfd*/,
    const boost::system::error_code& ec)
{
    if (ec)
    {
        BMCWEB_LOG_ERROR(
            "switch-config POST [onAddConfigFileReply]: D-Bus call "
            "failed for fabric {}: {}",
            fabricId, ec.message());
        handleAddConfigFileError(asyncResp, uploadUri, ec);
        return;
    }
    BMCWEB_LOG_INFO(
        "switch-config POST [onAddConfigFileReply]: succeeded for fabric {}",
        fabricId);
    asyncResp->res.result(boost::beast::http::status::no_content);
}

/**
 * @brief Invoke AddConfigFile(h) once the updater service is resolved.
 *
 * Passed as the SwitchCfgCallback to switchCfgResolveUpdater for POST.
 */
inline void onUpdaterResolvedForPost(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& fabricId, const std::string& uploadUri,
    const std::shared_ptr<SwitchCfgMemFd>& memfd, const std::string& service,
    const std::string& updaterObjPath)
{
    BMCWEB_LOG_DEBUG(
        "switch-config POST [onUpdaterResolvedForPost]: calling AddConfigFile "
        "service={} path={} fabric={}",
        service, updaterObjPath, fabricId);
    dbus::utility::async_method_call(
        [asyncResp, fabricId, uploadUri,
         memfd](const boost::system::error_code& ec) {
            onAddConfigFileReply(asyncResp, fabricId, uploadUri, memfd, ec);
        },
        service, updaterObjPath, std::string(kSwitchCfgUpdaterIface),
        "AddConfigFile", sdbusplus::message::unix_fd(memfd->fd));
}

/**
 * @brief Handle the getSubTree reply and kick off updater resolution for POST.
 *
 * Locates the fabric's D-Bus object path from the subtree result, then calls
 * switchCfgResolveUpdater with onUpdaterResolvedForPost as the callback.
 */
inline void onFabricFoundForPost(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& fabricId, const std::string& uploadUri,
    const std::shared_ptr<SwitchCfgMemFd>& memfd,
    const boost::system::error_code& ec,
    const dbus::utility::MapperGetSubTreeResponse& subtree)
{
    if (ec)
    {
        BMCWEB_LOG_ERROR(
            "switch-config POST [onFabricFoundForPost]: getSubTree D-Bus "
            "error for {}: {}",
            kFabricIface, ec.message());
        messages::internalError(asyncResp->res);
        return;
    }

    std::string fabricObjPath;
    for (const auto& [path, connNames] : subtree)
    {
        auto pos = path.rfind('/');
        std::string_view leaf = (pos != std::string::npos)
                                    ? std::string_view(path).substr(pos + 1)
                                    : std::string_view(path);
        if (leaf == fabricId)
        {
            fabricObjPath = path;
            break;
        }
    }
    if (fabricObjPath.empty())
    {
        BMCWEB_LOG_ERROR(
            "switch-config POST [onFabricFoundForPost]: fabric '{}' "
            "not found in inventory",
            fabricId);
        messages::resourceNotFound(asyncResp->res, "Fabric", fabricId);
        return;
    }

    switchCfgResolveUpdater(asyncResp, fabricId, fabricObjPath, "POST",
                            std::bind_front(onUpdaterResolvedForPost, asyncResp,
                                            fabricId, uploadUri, memfd));
}

// ---------------------------------------------------------------------------
// DELETE async chain — remove uploaded switch configuration
// ---------------------------------------------------------------------------

/**
 * @brief Handle the RemoveConfigFile D-Bus reply.
 *
 * The D-Bus service throws when no config file is present; that maps to 404.
 */
inline void onRemoveConfigFileReply(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& fabricId, const boost::system::error_code& ec)
{
    if (ec)
    {
        BMCWEB_LOG_ERROR(
            "switch-config DELETE [onRemoveConfigFileReply]: D-Bus call "
            "failed for fabric {}: {}",
            fabricId, ec.message());
        messages::resourceNotFound(asyncResp->res, "SwitchConfig", fabricId);
        return;
    }
    BMCWEB_LOG_INFO(
        "switch-config DELETE [onRemoveConfigFileReply]: succeeded for "
        "fabric {}",
        fabricId);
    asyncResp->res.result(boost::beast::http::status::no_content);
}

/**
 * @brief Invoke RemoveConfigFile() once the updater service is resolved.
 *
 * Passed as the SwitchCfgCallback to switchCfgResolveUpdater for DELETE.
 */
inline void onUpdaterResolvedForDelete(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& fabricId, const std::string& service,
    const std::string& updaterObjPath)
{
    BMCWEB_LOG_DEBUG(
        "switch-config DELETE [onUpdaterResolvedForDelete]: calling "
        "RemoveConfigFile service={} path={} fabric={}",
        service, updaterObjPath, fabricId);

    dbus::utility::async_method_call(
        [asyncResp, fabricId](const boost::system::error_code& ec) {
            onRemoveConfigFileReply(asyncResp, fabricId, ec);
        },
        service, updaterObjPath, std::string(kSwitchCfgUpdaterIface),
        "RemoveConfigFile");
}

/**
 * @brief Handle the getSubTree reply and kick off updater resolution for
 * DELETE.
 *
 * Locates the fabric's D-Bus object path from the subtree result, then calls
 * switchCfgResolveUpdater with onUpdaterResolvedForDelete as the callback.
 */
inline void onFabricFoundForDelete(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& fabricId, const boost::system::error_code& ec,
    const dbus::utility::MapperGetSubTreeResponse& subtree)
{
    if (ec)
    {
        BMCWEB_LOG_ERROR(
            "switch-config DELETE [onFabricFoundForDelete]: getSubTree "
            "D-Bus error for {}: {}",
            kFabricIface, ec.message());
        messages::internalError(asyncResp->res);
        return;
    }

    std::string fabricObjPath;
    for (const auto& [path, connNames] : subtree)
    {
        auto pos = path.rfind('/');
        std::string_view leaf = (pos != std::string::npos)
                                    ? std::string_view(path).substr(pos + 1)
                                    : std::string_view(path);
        if (leaf == fabricId)
        {
            fabricObjPath = path;
            break;
        }
    }
    if (fabricObjPath.empty())
    {
        BMCWEB_LOG_ERROR(
            "switch-config DELETE [onFabricFoundForDelete]: fabric '{}' "
            "not found in inventory",
            fabricId);
        messages::resourceNotFound(asyncResp->res, "Fabric", fabricId);
        return;
    }

    switchCfgResolveUpdater(
        asyncResp, fabricId, fabricObjPath, "DELETE",
        std::bind_front(onUpdaterResolvedForDelete, asyncResp, fabricId));
}

// ---------------------------------------------------------------------------
// POST handler — upload binary switch configuration
// ---------------------------------------------------------------------------

/**
 * @brief POST /redfish/v1/Fabrics/<fabricId>/upload-switch-config
 *
 * Validates the request (body size, Content-Type, multipart structure), writes
 * the file content to a memfd, then dispatches the async D-Bus chain:
 *   onFabricFoundForPost → switchCfgResolveUpdater →
 *   onUpdaterResolvedForPost → AddConfigFile(h) → onAddConfigFileReply
 *
 * Returns HTTP 204 on success.
 */
inline void handleConfigFilePost(
    App& app, crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& fabricId)
{
    if (!redfish::setUpRedfishRoute(app, req, asyncResp))
    {
        BMCWEB_LOG_ERROR(
            "switch-config POST [handleConfigFilePost]: setUpRedfishRoute "
            "rejected request for fabric {}",
            fabricId);
        return;
    }

    if (req.body().size() > switchConfigBodyLimit)
    {
        BMCWEB_LOG_ERROR(
            "switch-config POST [handleConfigFilePost]: body too large "
            "({} bytes) for fabric {}",
            req.body().size(), fabricId);
        messages::payloadTooLarge(asyncResp->res);
        return;
    }

    const std::string uploadUri =
        "/redfish/v1/Fabrics/" + fabricId + "/upload-switch-config";

    auto importFileContent = scanMimePartsForImportFile(
        req.multipart(), asyncResp, fabricId, uploadUri);
    if (!importFileContent)
    {
        return;
    }

    auto memfd = std::make_shared<SwitchCfgMemFd>();
    if (memfd->fd == -1)
    {
        BMCWEB_LOG_ERROR(
            "switch-config POST [handleConfigFilePost]: memfd_create "
            "failed for fabric {}",
            fabricId);
        messages::internalError(asyncResp->res);
        return;
    }

    ssize_t written =
        write(memfd->fd, importFileContent->data(), importFileContent->size());
    if (written != static_cast<ssize_t>(importFileContent->size()))
    {
        BMCWEB_LOG_ERROR(
            "switch-config POST [handleConfigFilePost]: memfd write failed "
            "({}/{} bytes) for fabric {}",
            written, importFileContent->size(), fabricId);
        messages::internalError(asyncResp->res);
        return;
    }

    if (!memfd->rewind())
    {
        BMCWEB_LOG_ERROR(
            "switch-config POST [handleConfigFilePost]: memfd rewind "
            "failed for fabric {}",
            fabricId);
        messages::internalError(asyncResp->res);
        return;
    }

    dbus::utility::getSubTree("/xyz/openbmc_project/inventory", 0,
                              std::array<std::string_view, 1>{kFabricIface},
                              std::bind_front(onFabricFoundForPost, asyncResp,
                                              fabricId, uploadUri, memfd));
}

// ---------------------------------------------------------------------------
// DELETE handler — remove uploaded switch configuration
// ---------------------------------------------------------------------------

/**
 * @brief DELETE /redfish/v1/Fabrics/<fabricId>/upload-switch-config
 *
 * Dispatches the async D-Bus chain:
 *   onFabricFoundForDelete → switchCfgResolveUpdater →
 *   onUpdaterResolvedForDelete → RemoveConfigFile() → onRemoveConfigFileReply
 *
 * Returns HTTP 204 on success, 404 when the D-Bus service reports no file.
 */
inline void handleConfigFileDelete(
    App& app, const crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& fabricId)
{
    if (!redfish::setUpRedfishRoute(app, req, asyncResp))
    {
        BMCWEB_LOG_ERROR(
            "switch-config DELETE [handleConfigFileDelete]: setUpRedfishRoute "
            "rejected request for fabric {}",
            fabricId);
        return;
    }

    dbus::utility::getSubTree(
        "/xyz/openbmc_project/inventory", 0,
        std::array<std::string_view, 1>{kFabricIface},
        std::bind_front(onFabricFoundForDelete, asyncResp, fabricId));
}

// ---------------------------------------------------------------------------
// Route registration
// ---------------------------------------------------------------------------

/**
 * @brief Register POST and DELETE routes for the switch config push URI.
 *
 * Called from requestRoutesNvidia() in redfish-core/src/redfish_nvidia.cpp
 * alongside requestRoutesFabric().  Note: fabric.hpp already includes this
 * header (for addSwitchConfigPushURI), so no extra #include is needed in
 * redfish_nvidia.cpp.
 */
inline void requestRoutesNvidiaConfigFile(App& app)
{
    BMCWEB_ROUTE(app, "/redfish/v1/Fabrics/<str>/upload-switch-config/")
        .privileges(redfish::privileges::postUpdateService)
        .methods(boost::beast::http::verb::post)(
            std::bind_front(handleConfigFilePost, std::ref(app)));

    BMCWEB_ROUTE(app, "/redfish/v1/Fabrics/<str>/upload-switch-config/")
        .privileges(redfish::privileges::deleteUpdateService)
        .methods(boost::beast::http::verb::delete_)(
            std::bind_front(handleConfigFileDelete, std::ref(app)));
}

} // namespace redfish
