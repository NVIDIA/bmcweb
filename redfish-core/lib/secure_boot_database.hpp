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

#include "certificate_service.hpp"
#include "nlohmann/json.hpp"
#include "task.hpp"

#include <app.hpp>
#include <dbus_utility.hpp>
#include <query.hpp>
#include <registries/privilege_registry.hpp>
#include <utils/privilege_utils.hpp>

#include <fstream>
#include <iostream>
#include <regex>

namespace redfish
{
namespace secure_boot
{

const std::string signatureFormatPrefix =
    "xyz.openbmc_project.BIOSConfig.SecureBootDatabase.Signature.SignatureFormat.";
const std::vector<std::string> validSignatureFormat = {
    "EFI_CERT_SHA256_GUID",         "EFI_CERT_RSA2048_GUID",
    "EFI_CERT_RSA2048_SHA256_GUID", "EFI_CERT_SHA1_GUID",
    "EFI_CERT_RSA2048_SHA1_GUID",   "EFI_CERT_X509_GUID",
    "EFI_CERT_SHA224_GUID",         "EFI_CERT_SHA384_GUID",
    "EFI_CERT_SHA512_GUID",         "EFI_CERT_X509_SHA256_GUID",
    "EFI_CERT_X509_SHA384_GUID",    "EFI_CERT_X509_SHA512_GUID",
    "EFI_CERT_TYPE_PKCS7_GUID"};

inline std::string signatureFormatDbusToRf(const std::string& dbusString)
{
    if (dbusString.starts_with(signatureFormatPrefix))
    {
        return dbusString.substr(signatureFormatPrefix.size());
    }
    return "";
}

inline std::string signatureFormatRfToDbus(const std::string& rfString)
{
    const auto& v = validSignatureFormat;
    if (std::find(v.begin(), v.end(), rfString) != v.end())
    {
        return signatureFormatPrefix + rfString;
    }
    return "";
}

inline std::string capitalize(const std::string& s)
{
    std::string res = s;
    if (!res.empty())
    {
        res[0] = static_cast<char>(std::toupper(res[0]));
    }
    return res;
}

inline std::string getServiceName(const std::string& databaseId)
{
    return std::string("xyz.openbmc_project.Certs.Manager.SecureBootDatabase." +
                       capitalize(databaseId));
}

inline std::string getCertObjectPath(const std::string& databaseId,
                                     const std::string& certId = "")
{
    if (certId.empty())
    {
        return std::string(
            "/xyz/openbmc_project/secureBootDatabase/" + databaseId);
    }
    return std::string("/xyz/openbmc_project/secureBootDatabase/" + databaseId +
                       "/certs/" + certId);
}

inline std::string getSigObjectPath(const std::string& databaseId,
                                    const std::string& sigId = "")
{
    if (sigId.empty())
    {
        return std::string(
            "/xyz/openbmc_project/secureBootDatabase/" + databaseId);
    }
    return std::string("/xyz/openbmc_project/secureBootDatabase/" + databaseId +
                       "/signature/" + sigId);
}

inline bool isDefaultDatabase(const std::string& databaseId)
{
    return regex_match(databaseId, std::regex(R"(.*Default)"));
}

inline bool hasSignature(const std::string& databaseId)
{
    return !regex_match(databaseId, std::regex(R"((PK|KEK).*)"));
}

inline void createPendingRequest(
    const crow::Request& req, const std::shared_ptr<bmcweb::AsyncResp>& aResp)
{
    auto task = task::TaskData::createTask(
        [](boost::system::error_code, sdbusplus::message_t&,
           const std::shared_ptr<task::TaskData>&) { return false; },
        "0");
    task->payload.emplace(req);
    task->state = "Pending";
    task->populateResp(aResp->res);
    return;
}

inline void handleSecureBootDatabaseCollectionGet(
    crow::App& app, const crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& aResp,
    [[maybe_unused]] const std::string& systemName)
{
    if (!redfish::setUpRedfishRoute(app, req, aResp))
    {
        return;
    }
    aResp->res.jsonValue["@odata.id"] =
        "/redfish/v1/Systems/" + std::string(BMCWEB_REDFISH_SYSTEM_URI_NAME) +
        "/SecureBoot/SecureBootDatabases";
    aResp->res.jsonValue["@odata.type"] =
        "#SecureBootDatabaseCollection.SecureBootDatabaseCollection";
    aResp->res.jsonValue["Name"] = "UEFI SecureBoot Database Collection";

    crow::connections::systemBus->async_method_call(
        [aResp](const boost::system::error_code ec,
                const dbus::utility::MapperGetSubTreePathsResponse& objects) {
            if (ec == boost::system::errc::io_error)
            {
                aResp->res.jsonValue["Members"] = nlohmann::json::array();
                aResp->res.jsonValue["Members@odata.count"] = 0;
                return;
            }

            if (ec)
            {
                BMCWEB_LOG_DEBUG("DBUS response error {}", ec.value());
                messages::internalError(aResp->res);
                return;
            }

            std::vector<std::string> pathNames;
            for (const auto& object : objects)
            {
                sdbusplus::message::object_path path(object);
                std::string leaf = path.filename();
                if (leaf == "certs")
                {
                    leaf = path.parent_path().filename();
                }
                if (leaf.empty())
                {
                    continue;
                }
                pathNames.push_back(leaf);
            }
            std::sort(pathNames.begin(), pathNames.end(),
                      AlphanumLess<std::string>());

            nlohmann::json& members = aResp->res.jsonValue["Members"];
            members = nlohmann::json::array();
            for (const std::string& leaf : pathNames)
            {
                std::string newPath =
                    "/redfish/v1/Systems/" +
                    std::string(BMCWEB_REDFISH_SYSTEM_URI_NAME) +
                    "/SecureBoot/SecureBootDatabases";
                newPath += '/';
                newPath += leaf;
                nlohmann::json::object_t member;
                member["@odata.id"] = std::move(newPath);
                members.push_back(std::move(member));
            }
            aResp->res.jsonValue["Members@odata.count"] = members.size();
        },
        "xyz.openbmc_project.ObjectMapper",
        "/xyz/openbmc_project/object_mapper",
        "xyz.openbmc_project.ObjectMapper", "GetSubTreePaths",
        "/xyz/openbmc_project/secureBootDatabase/", 0,
        std::array<const char*, 1>{"xyz.openbmc_project.Certs.Install"});
}

inline void handleSecureBootDatabaseGet(
    crow::App& app, const crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& aResp,
    [[maybe_unused]] const std::string& systemName,
    const std::string& databaseId)
{
    if (!redfish::setUpRedfishRoute(app, req, aResp))
    {
        return;
    }
    aResp->res.jsonValue["@odata.id"] =
        "/redfish/v1/Systems/" + std::string(BMCWEB_REDFISH_SYSTEM_URI_NAME) +
        "/SecureBoot/SecureBootDatabases/" + databaseId;
    aResp->res.jsonValue["@odata.type"] =
        "#SecureBootDatabase.v1_0_1.SecureBootDatabase";
    aResp->res.jsonValue["Id"] = databaseId;
    aResp->res.jsonValue["Name"] = databaseId + " Database";
    aResp->res.jsonValue["Description"] =
        "UEFI " + databaseId + " Secure Boot Database";
    aResp->res.jsonValue["DatabaseId"] = databaseId;
    aResp->res.jsonValue["Certificates"]["@odata.id"] =
        "/redfish/v1/Systems/" + std::string(BMCWEB_REDFISH_SYSTEM_URI_NAME) +
        "/SecureBoot/SecureBootDatabases/" + databaseId + "/Certificates";
    if (hasSignature(databaseId))
    {
        aResp->res.jsonValue["Signatures"]["@odata.id"] =
            "/redfish/v1/Systems/" +
            std::string(BMCWEB_REDFISH_SYSTEM_URI_NAME) +
            "/SecureBoot/SecureBootDatabases/" + databaseId + "/Signatures";
    }
    if (!isDefaultDatabase(databaseId))
    {
        aResp->res
            .jsonValue["Actions"]["#SecureBootDatabase.ResetKeys"]["target"] =
            "/redfish/v1/Systems/" +
            std::string(BMCWEB_REDFISH_SYSTEM_URI_NAME) +
            "/SecureBoot/SecureBootDatabases/" + databaseId +
            "/Actions/SecureBootDatabase.ResetKeys";
        aResp->res.jsonValue["Actions"]["#SecureBootDatabase.ResetKeys"]
                            ["ResetKeysType@Redfish.AllowableValues"] = {
            "DeleteAllKeys"};

        // Add "ResetAllKeysToDefault" type if and only if we have PKDefault
        // certificate
        crow::connections::systemBus->async_method_call(
            [aResp](
                const boost::system::error_code ec,
                const dbus::utility::MapperGetSubTreePathsResponse& objects) {
                if (ec)
                {
                    BMCWEB_LOG_DEBUG("DBUS response error {}", ec.value());
                    // No error if default database does not exist
                    return;
                }

                if (objects.size() > 0)
                {
                    aResp->res
                        .jsonValue["Actions"]["#SecureBootDatabase.ResetKeys"]
                                  ["ResetKeysType@Redfish.AllowableValues"] = {
                        "ResetAllKeysToDefault", "DeleteAllKeys"};
                }
            },
            "xyz.openbmc_project.ObjectMapper",
            "/xyz/openbmc_project/object_mapper",
            "xyz.openbmc_project.ObjectMapper", "GetSubTreePaths",
            "/xyz/openbmc_project/secureBootDatabase/PKDefault/certs", 0,
            std::array<const char*, 1>{
                "xyz.openbmc_project.Certs.Certificate"});
    }
}

inline void handleSecureBootDatabaseResetKeys(
    crow::App& app, const crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& aResp,
    [[maybe_unused]] const std::string& systemName,
    const std::string& databaseId)
{
    if (!redfish::setUpRedfishRoute(app, req, aResp))
    {
        return;
    }

    std::string resetKeysType;
    if (!json_util::readJsonAction(req, aResp->res, "ResetKeysType",
                                   resetKeysType))
    {
        BMCWEB_LOG_DEBUG("SecureBootDatabase Action readJsonAction error");
        return;
    }

    if (resetKeysType != "ResetAllKeysToDefault" &&
        resetKeysType != "DeleteAllKeys")
    {
        messages::propertyValueNotInList(aResp->res, resetKeysType,
                                         "ResetKeysType");
        return;
    }

    privilege_utils::isBiosPrivilege(
        req, [req, aResp, databaseId](const boost::system::error_code ec,
                                      const bool isBios) {
            if (ec)
            {
                messages::internalError(aResp->res);
                return;
            }
            if (isBios == false)
            {
                if (isDefaultDatabase(databaseId))
                {
                    messages::insufficientPrivilege(aResp->res);
                    return;
                }
                createPendingRequest(req, aResp);
                return;
            }

            // BIOS does use action. It DELETE and POST certificates and
            // signatures
            messages::actionNotSupported(aResp->res, "ResetKeys");
            return;
        });
}

inline void handleCertificateCollectionGet(
    crow::App& app, const crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& aResp,
    [[maybe_unused]] const std::string& systemName,
    const std::string& databaseId)
{
    if (!redfish::setUpRedfishRoute(app, req, aResp))
    {
        return;
    }
    aResp->res.jsonValue["@odata.id"] =
        "/redfish/v1/Systems/" + std::string(BMCWEB_REDFISH_SYSTEM_URI_NAME) +
        "/SecureBoot/SecureBootDatabases/" + databaseId + "/Certificates";
    aResp->res.jsonValue["@odata.type"] =
        "#CertificateCollection.CertificateCollection";
    aResp->res.jsonValue["Name"] = "Certificate Collection";
    aResp->res.jsonValue["@Redfish.SupportedCertificates"] = {"PEM"};
    std::string certURI =
        "/redfish/v1/Systems/" + std::string(BMCWEB_REDFISH_SYSTEM_URI_NAME) +
        "/SecureBoot/SecureBootDatabases/" + databaseId + "/Certificates";
    constexpr std::array<std::string_view, 1> interfaces{
        "xyz.openbmc_project.Certs.Certificate"};
    collection_util::getCollectionMembers(
        aResp, boost::urls::url(certURI), interfaces,
        std::string(
            "/xyz/openbmc_project/secureBootDatabase/" + databaseId + "/certs")
            .c_str());
}

inline void handleCertificateCollectionPost(
    crow::App& app, const crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& aResp,
    [[maybe_unused]] const std::string& systemName,
    const std::string& databaseId)
{
    if (!redfish::setUpRedfishRoute(app, req, aResp))
    {
        return;
    }

    std::string certString;
    std::string certType;
    std::optional<std::string> owner;
    if (!json_util::readJsonPatch(req, aResp->res, "CertificateString",
                                  certString, "CertificateType", certType,
                                  "UefiSignatureOwner", owner))
    {
        BMCWEB_LOG_DEBUG("Certificate POST readJsonPatch error");
        return;
    }

    if (certString.size() == 0)
    {
        messages::propertyValueIncorrect(aResp->res, "CertificateString",
                                         certString);
        return;
    }

    if (certType != "PEM")
    {
        messages::propertyValueNotInList(aResp->res, certType,
                                         "CertificateType");
        return;
    }

    privilege_utils::isBiosPrivilege(
        req, [req, aResp, databaseId, certString,
              owner](const boost::system::error_code ec, const bool isBios) {
            if (ec)
            {
                messages::internalError(aResp->res);
                return;
            }
            if (isBios == false)
            {
                if (isDefaultDatabase(databaseId))
                {
                    messages::insufficientPrivilege(aResp->res);
                    return;
                }
                createPendingRequest(req, aResp);
                return;
            }

            std::shared_ptr<CertificateFile> certFile =
                std::make_shared<CertificateFile>(certString);

            crow::connections::systemBus->async_method_call(
                [aResp, databaseId, owner,
                 certFile](const boost::system::error_code ec,
                           const std::string& objectPath) {
                    if (ec)
                    {
                        BMCWEB_LOG_ERROR("DBUS response error: {}", ec);
                        messages::internalError(aResp->res);
                        return;
                    }

                    sdbusplus::message::object_path path(objectPath);
                    std::string certId = path.filename();
                    messages::created(aResp->res);
                    aResp->res.addHeader(
                        boost::beast::http::field::location,
                        "/redfish/v1/Systems/" +
                            std::string(BMCWEB_REDFISH_SYSTEM_URI_NAME) +
                            "/SecureBoot/SecureBootDatabases/" + databaseId +
                            "/Certificates/" + certId);

                    if (owner)
                    {
                        crow::connections::systemBus->async_method_call(
                            [aResp](const boost::system::error_code ec) {
                                if (ec)
                                {
                                    BMCWEB_LOG_ERROR("DBUS response error: {}",
                                                     ec);
                                    messages::internalError(aResp->res);
                                    return;
                                }
                            },
                            getServiceName(databaseId), objectPath,
                            "org.freedesktop.DBus.Properties", "Set",
                            "xyz.openbmc_project.Common.UUID", "UUID",
                            dbus::utility::DbusVariantType(*owner));
                    }
                },
                getServiceName(databaseId), getCertObjectPath(databaseId),
                "xyz.openbmc_project.Certs.Install", "Install",
                certFile->getCertFilePath());
        });
}

inline void handleCertificateGet(
    crow::App& app, const crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& aResp,
    [[maybe_unused]] const std::string& systemName,
    const std::string& databaseId, const std::string& certId)
{
    if (!redfish::setUpRedfishRoute(app, req, aResp))
    {
        return;
    }
    aResp->res.jsonValue["@odata.id"] =
        "/redfish/v1/Systems/" + std::string(BMCWEB_REDFISH_SYSTEM_URI_NAME) +
        "/SecureBoot/SecureBootDatabases/" + databaseId + "/Certificates/" +
        certId;
    aResp->res.jsonValue["@odata.type"] = "#Certificate.v1_7_0.Certificate";
    aResp->res.jsonValue["Id"] = certId;
    aResp->res.jsonValue["Name"] = databaseId + " Certificate";

    sdbusplus::asio::getAllProperties(
        *crow::connections::systemBus, getServiceName(databaseId),
        getCertObjectPath(databaseId, certId), "",
        [aResp,
         certId](const boost::system::error_code ec,
                 const dbus::utility::DBusPropertiesMap& propertiesList) {
            if (ec)
            {
                BMCWEB_LOG_ERROR("DBUS response error: {}", ec);
                messages::resourceNotFound(aResp->res, "Certificate", certId);
                return;
            }

            const std::string* certificateString = nullptr;
            const std::vector<std::string>* keyUsage = nullptr;
            const std::string* issuer = nullptr;
            const std::string* subject = nullptr;
            const uint64_t* validNotAfter = nullptr;
            const uint64_t* validNotBefore = nullptr;
            const std::string* owner = nullptr;

            const bool success = sdbusplus::unpackPropertiesNoThrow(
                dbus_utils::UnpackErrorPrinter(), propertiesList,
                "CertificateString", certificateString, "KeyUsage", keyUsage,
                "Issuer", issuer, "Subject", subject, "ValidNotAfter",
                validNotAfter, "ValidNotBefore", validNotBefore, "UUID", owner);

            if (!success)
            {
                messages::internalError(aResp->res);
                return;
            }

            aResp->res.jsonValue["CertificateString"] = "";
            aResp->res.jsonValue["KeyUsage"] = nlohmann::json::array();

            if (certificateString != nullptr)
            {
                aResp->res.jsonValue["CertificateString"] = *certificateString;
                aResp->res.jsonValue["CertificateType"] = "PEM";
            }

            if (keyUsage != nullptr)
            {
                aResp->res.jsonValue["KeyUsage"] = *keyUsage;
            }

            if (issuer != nullptr)
            {
                updateCertIssuerOrSubject(aResp->res.jsonValue["Issuer"],
                                          *issuer);
            }

            if (subject != nullptr)
            {
                updateCertIssuerOrSubject(aResp->res.jsonValue["Subject"],
                                          *subject);
            }

            if (validNotAfter != nullptr)
            {
                aResp->res.jsonValue["ValidNotAfter"] =
                    redfish::time_utils::getDateTimeUint(*validNotAfter);
            }

            if (validNotBefore != nullptr)
            {
                aResp->res.jsonValue["ValidNotBefore"] =
                    redfish::time_utils::getDateTimeUint(*validNotBefore);
            }

            if (owner != nullptr)
            {
                aResp->res.jsonValue["UefiSignatureOwner"] = *owner;
            }
        });
}

inline void handleCertificateDelete(
    crow::App& app, const crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& aResp,
    [[maybe_unused]] const std::string& systemName,
    const std::string& databaseId, const std::string& certId)
{
    if (!redfish::setUpRedfishRoute(app, req, aResp))
    {
        return;
    }

    privilege_utils::isBiosPrivilege(
        req, [req, aResp, databaseId,
              certId](const boost::system::error_code ec, const bool isBios) {
            if (ec)
            {
                messages::internalError(aResp->res);
                return;
            }
            if (isBios == false)
            {
                if (isDefaultDatabase(databaseId))
                {
                    messages::insufficientPrivilege(aResp->res);
                    return;
                }
                createPendingRequest(req, aResp);
                return;
            }

            crow::connections::systemBus->async_method_call(
                [aResp](const boost::system::error_code ec) {
                    if (ec)
                    {
                        messages::internalError(aResp->res);
                        return;
                    }
                    aResp->res.result(boost::beast::http::status::no_content);
                },
                getServiceName(databaseId),
                getCertObjectPath(databaseId, certId),
                "xyz.openbmc_project.Object.Delete", "Delete");
        });
}

inline void handleSignatureCollectionGet(
    crow::App& app, const crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& aResp,
    [[maybe_unused]] const std::string& systemName,
    const std::string& databaseId)
{
    if (!redfish::setUpRedfishRoute(app, req, aResp))
    {
        return;
    }
    if (!hasSignature(databaseId))
    {
        messages::resourceNotFound(aResp->res, "SignatureCollection",
                                   databaseId);
        return;
    }
    aResp->res.jsonValue["@odata.id"] =
        "/redfish/v1/Systems/" + std::string(BMCWEB_REDFISH_SYSTEM_URI_NAME) +
        "/SecureBoot/SecureBootDatabases/" + databaseId + "/Signatures";
    aResp->res.jsonValue["@odata.type"] =
        "#SignatureCollection.SignatureCollection";
    aResp->res.jsonValue["Name"] = "Signature Collection";

    std::string signatureURL =
        "/redfish/v1/Systems/" + std::string(BMCWEB_REDFISH_SYSTEM_URI_NAME) +
        "/SecureBoot/SecureBootDatabases/" + databaseId + "/Signatures";
    constexpr std::array<std::string_view, 1> interfaces{
        "xyz.openbmc_project.BIOSConfig.SecureBootDatabase.Signature"};
    collection_util::getCollectionMembers(
        aResp, boost::urls::url(signatureURL), interfaces,
        std::string("/xyz/openbmc_project/secureBootDatabase/" + databaseId +
                    "/signature")
            .c_str());
}

inline void handleSignatureCollectionPost(
    crow::App& app, const crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& aResp,
    [[maybe_unused]] const std::string& systemName,
    const std::string& databaseId)
{
    if (!redfish::setUpRedfishRoute(app, req, aResp))
    {
        return;
    }
    if (!hasSignature(databaseId))
    {
        messages::resourceNotFound(aResp->res, "SignatureCollection",
                                   databaseId);
        return;
    }

    std::string sigString;
    std::string sigType;
    std::string sigTypeRegistry;
    std::optional<std::string> owner;
    if (!json_util::readJsonPatch(
            req, aResp->res, "SignatureString", sigString, "SignatureType",
            sigType, "SignatureTypeRegistry", sigTypeRegistry,
            "UefiSignatureOwner", owner))
    {
        BMCWEB_LOG_DEBUG("Certificate POST readJsonPatch error");
        return;
    }

    if (sigString.size() == 0)
    {
        messages::propertyValueIncorrect(aResp->res, "SignatureString",
                                         sigString);
        return;
    }

    auto sigTypeDbus = signatureFormatRfToDbus(sigType);
    if (sigTypeDbus.size() == 0)
    {
        messages::propertyValueNotInList(aResp->res, sigType, "SignatureType");
        return;
    }

    if (sigTypeRegistry != "UEFI")
    {
        messages::propertyValueNotInList(aResp->res, sigTypeRegistry,
                                         "SignatureTypeRegistry");
        return;
    }

    privilege_utils::isBiosPrivilege(req, [req, aResp, databaseId, sigString,
                                           sigTypeDbus, owner](
                                              const boost::system::error_code
                                                  ec,
                                              const bool isBios) {
        if (ec)
        {
            messages::internalError(aResp->res);
            return;
        }
        if (isBios == false)
        {
            if (isDefaultDatabase(databaseId))
            {
                messages::insufficientPrivilege(aResp->res);
                return;
            }
            createPendingRequest(req, aResp);
            return;
        }

        crow::connections::systemBus->async_method_call(
            [aResp, databaseId, owner](const boost::system::error_code ec,
                                       const std::string& objectPath) {
                if (ec)
                {
                    BMCWEB_LOG_ERROR("DBUS response error: {}", ec);
                    messages::internalError(aResp->res);
                    return;
                }

                sdbusplus::message::object_path path(objectPath);
                std::string sigId = path.filename();
                messages::created(aResp->res);
                aResp->res.addHeader(
                    boost::beast::http::field::location,
                    "/redfish/v1/Systems/" +
                        std::string(BMCWEB_REDFISH_SYSTEM_URI_NAME) +
                        "/SecureBoot/SecureBootDatabases/" + databaseId +
                        "/Signatures/" + sigId);

                if (owner)
                {
                    crow::connections::systemBus->async_method_call(
                        [aResp](const boost::system::error_code ec) {
                            if (ec)
                            {
                                BMCWEB_LOG_ERROR("DBUS response error: {}", ec);
                                messages::internalError(aResp->res);
                                return;
                            }
                        },
                        getServiceName(databaseId), objectPath,
                        "org.freedesktop.DBus.Properties", "Set",
                        "xyz.openbmc_project.Common.UUID", "UUID",
                        dbus::utility::DbusVariantType(*owner));
                }
            },
            getServiceName(databaseId), getSigObjectPath(databaseId),
            "xyz.openbmc_project.BIOSConfig.SecureBootDatabase.AddSignature",
            "Add", sigString, sigTypeDbus);
    });
}

inline void handleSignatureGet(crow::App& app, const crow::Request& req,
                               const std::shared_ptr<bmcweb::AsyncResp>& aResp,
                               [[maybe_unused]] const std::string& systemName,
                               const std::string& databaseId,
                               const std::string& sigId)
{
    if (!redfish::setUpRedfishRoute(app, req, aResp))
    {
        return;
    }
    if (!hasSignature(databaseId))
    {
        messages::resourceNotFound(aResp->res, "Signature", sigId);
        return;
    }
    aResp->res.jsonValue["@odata.id"] =
        "/redfish/v1/Systems/" + std::string(BMCWEB_REDFISH_SYSTEM_URI_NAME) +
        "/SecureBoot/SecureBootDatabases/" + databaseId + "/Signatures/" +
        sigId;
    aResp->res.jsonValue["@odata.type"] = "#Signature.v1_0_2.Signature";
    aResp->res.jsonValue["Name"] = "Signature Collection";
    aResp->res.jsonValue["Id"] = sigId;
    aResp->res.jsonValue["Name"] = databaseId + " Signature";

    sdbusplus::asio::getAllProperties(
        *crow::connections::systemBus, getServiceName(databaseId),
        getSigObjectPath(databaseId, sigId), "",
        [aResp, sigId](const boost::system::error_code ec,
                       const dbus::utility::DBusPropertiesMap& propertiesList) {
            if (ec)
            {
                BMCWEB_LOG_ERROR("DBUS response error: {}", ec);
                messages::resourceNotFound(aResp->res, "Signature", sigId);
                return;
            }

            const std::string* signatureString = nullptr;
            const std::string* format = nullptr;
            const std::string* owner = nullptr;

            const bool success = sdbusplus::unpackPropertiesNoThrow(
                dbus_utils::UnpackErrorPrinter(), propertiesList,
                "SignatureString", signatureString, "Format", format, "UUID",
                owner);

            if (!success)
            {
                messages::internalError(aResp->res);
                return;
            }

            aResp->res.jsonValue["SignatureString"] = "";
            aResp->res.jsonValue["SignatureTypeRegistry"] = "UEFI";

            if (signatureString != nullptr)
            {
                aResp->res.jsonValue["SignatureString"] = *signatureString;
            }

            if (format != nullptr)
            {
                aResp->res.jsonValue["SignatureType"] =
                    signatureFormatDbusToRf(*format);
            }

            if (owner != nullptr)
            {
                aResp->res.jsonValue["UefiSignatureOwner"] = *owner;
            }
        });
}

inline void handleSignatureDelete(
    crow::App& app, const crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& aResp,
    [[maybe_unused]] const std::string& systemName,
    const std::string& databaseId, const std::string& sigId)
{
    if (!redfish::setUpRedfishRoute(app, req, aResp))
    {
        return;
    }
    if (!hasSignature(databaseId))
    {
        messages::resourceNotFound(aResp->res, "Signature", sigId);
        return;
    }

    privilege_utils::isBiosPrivilege(
        req, [req, aResp, databaseId,
              sigId](const boost::system::error_code ec, const bool isBios) {
            if (ec)
            {
                messages::internalError(aResp->res);
                return;
            }
            if (isBios == false)
            {
                if (isDefaultDatabase(databaseId))
                {
                    messages::insufficientPrivilege(aResp->res);
                    return;
                }
                createPendingRequest(req, aResp);
                return;
            }

            crow::connections::systemBus->async_method_call(
                [aResp](const boost::system::error_code ec) {
                    if (ec)
                    {
                        messages::internalError(aResp->res);
                        return;
                    }
                    aResp->res.result(boost::beast::http::status::no_content);
                },
                getServiceName(databaseId), getSigObjectPath(databaseId, sigId),
                "xyz.openbmc_project.Object.Delete", "Delete");
        });
}

} // namespace secure_boot

inline void requestRoutesSecureBootDatabase(App& app)
{
    BMCWEB_ROUTE(app,
                 "/redfish/v1/Systems/<str>/SecureBoot/SecureBootDatabases/")
        .privileges(redfish::privileges::getSecureBootDatabaseCollection)
        .methods(boost::beast::http::verb::get)(std::bind_front(
            secure_boot::handleSecureBootDatabaseCollectionGet, std::ref(app)));

    BMCWEB_ROUTE(
        app, "/redfish/v1/Systems/<str>/SecureBoot/SecureBootDatabases/<str>/")
        .privileges(redfish::privileges::getSecureBootDatabase)
        .methods(boost::beast::http::verb::get)(std::bind_front(
            secure_boot::handleSecureBootDatabaseGet, std::ref(app)));

    BMCWEB_ROUTE(
        app,
        "/redfish/v1/Systems/<str>/SecureBoot/SecureBootDatabases/<str>/Actions/SecureBootDatabase.ResetKeys/")
        .privileges(redfish::privileges::postSecureBootDatabase)
        .methods(boost::beast::http::verb::post)(std::bind_front(
            secure_boot::handleSecureBootDatabaseResetKeys, std::ref(app)));

    BMCWEB_ROUTE(
        app,
        "/redfish/v1/Systems/<str>/SecureBoot/SecureBootDatabases/<str>/Certificates/")
        .privileges(redfish::privileges::getCertificateCollection)
        .methods(boost::beast::http::verb::get)(std::bind_front(
            secure_boot::handleCertificateCollectionGet, std::ref(app)));

    BMCWEB_ROUTE(
        app,
        "/redfish/v1/Systems/<str>/SecureBoot/SecureBootDatabases/<str>/Certificates/")
        .privileges(redfish::privileges::postCertificateCollection)
        .methods(boost::beast::http::verb::post)(std::bind_front(
            secure_boot::handleCertificateCollectionPost, std::ref(app)));

    BMCWEB_ROUTE(
        app,
        "/redfish/v1/Systems/<str>/SecureBoot/SecureBootDatabases/<str>/Certificates/<str>/")
        .privileges(redfish::privileges::getCertificate)
        .methods(boost::beast::http::verb::get)(
            std::bind_front(secure_boot::handleCertificateGet, std::ref(app)));

    BMCWEB_ROUTE(
        app,
        "/redfish/v1/Systems/<str>/SecureBoot/SecureBootDatabases/<str>/Certificates/<str>/")
        .privileges(redfish::privileges::deleteCertificate)
        .methods(boost::beast::http::verb::delete_)(std::bind_front(
            secure_boot::handleCertificateDelete, std::ref(app)));

    BMCWEB_ROUTE(
        app,
        "/redfish/v1/Systems/<str>/SecureBoot/SecureBootDatabases/<str>/Signatures/")
        .privileges(redfish::privileges::getSignatureCollection)
        .methods(boost::beast::http::verb::get)(std::bind_front(
            secure_boot::handleSignatureCollectionGet, std::ref(app)));

    BMCWEB_ROUTE(
        app,
        "/redfish/v1/Systems/<str>/SecureBoot/SecureBootDatabases/<str>/Signatures/")
        .privileges(redfish::privileges::postSignatureCollection)
        .methods(boost::beast::http::verb::post)(std::bind_front(
            secure_boot::handleSignatureCollectionPost, std::ref(app)));

    BMCWEB_ROUTE(
        app,
        "/redfish/v1/Systems/<str>/SecureBoot/SecureBootDatabases/<str>/Signatures/<str>/")
        .privileges(redfish::privileges::getSignature)
        .methods(boost::beast::http::verb::get)(
            std::bind_front(secure_boot::handleSignatureGet, std::ref(app)));

    BMCWEB_ROUTE(
        app,
        "/redfish/v1/Systems/<str>/SecureBoot/SecureBootDatabases/<str>/Signatures/<str>/")
        .privileges(redfish::privileges::deleteSignature)
        .methods(boost::beast::http::verb::delete_)(
            std::bind_front(secure_boot::handleSignatureDelete, std::ref(app)));
}

} // namespace redfish
