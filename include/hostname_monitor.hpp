// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: Copyright OpenBMC Authors
#pragma once
#include "dbus_singleton.hpp"
#include "dbus_utility.hpp"
#include "include/dbus_utility.hpp"
#include "logging.hpp"
#include "ssl_key_handler.hpp"

#include <openssl/asn1.h>
#include <openssl/crypto.h>
#include <openssl/evp.h>
#include <openssl/obj_mac.h>
#include <openssl/x509.h>
#include <systemd/sd-bus.h>

#include <boost/beast/core/file_posix.hpp>
#include <sdbusplus/bus/match.hpp>
#include <sdbusplus/message.hpp>

#include <array>
#include <cstddef>
#include <cstdlib>
#include <filesystem>
#include <iterator>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <variant>

namespace crow
{
namespace hostname_monitor
{
// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
static std::unique_ptr<sdbusplus::bus::match_t> hostnameSignalMonitor;

inline void installCertificate(const std::filesystem::path& certPath)
{
    dbus::utility::async_method_call(
        [certPath](const boost::system::error_code& ec) {
            if (ec)
            {
                BMCWEB_LOG_ERROR("Replace Certificate Fail..");
            }
            else
            {
                BMCWEB_LOG_INFO("Replace HTTPs Certificate Success, "
                                "remove temporary certificate file..");
            }
            std::error_code ec2;
            std::filesystem::remove(certPath, ec2);
            if (ec2)
            {
                BMCWEB_LOG_ERROR("Failed to remove certificate");
            }
        },
        "xyz.openbmc_project.Certs.Manager.Server.Https",
        "/xyz/openbmc_project/certs/server/https/1",
        "xyz.openbmc_project.Certs.Replace", "Replace", certPath.string());
}

inline std::optional<std::filesystem::path> writeCertToUniqueTempFile(
    const std::string& certData)
{
    std::error_code tempDirEc;
    std::filesystem::path tempDir =
        std::filesystem::temp_directory_path(tempDirEc);
    if (tempDirEc)
    {
        BMCWEB_LOG_ERROR("Failed to resolve temp directory");
        return std::nullopt;
    }
    std::string pathBuf = (tempDir / "hostname_cert_XXXXXX").string();

    int fd = ::mkstemp(pathBuf.data());
    if (fd == -1)
    {
        BMCWEB_LOG_ERROR("Failed to create temporary certificate file");
        return std::nullopt;
    }

    // file_posix used for managing RAII-close
    boost::beast::file_posix file;
    file.native_handle(fd);

    boost::system::error_code ec;
    size_t written = file.write(certData.data(), certData.size(), ec);
    bool writeFailed = ec || written != certData.size();

    boost::system::error_code closeEc;
    file.close(closeEc);
    if (closeEc)
    {
        writeFailed = true;
    }

    if (writeFailed)
    {
        BMCWEB_LOG_ERROR("Failed to write certificate to temp file");
        std::error_code ec2;
        std::filesystem::remove(pathBuf, ec2);
        if (ec2)
        {
            BMCWEB_LOG_ERROR("Failed to remove temp certificate file: {}",
                             ec2.message());
        }
        return std::nullopt;
    }

    return std::filesystem::path(pathBuf);
}

inline int onPropertyUpdate(sd_bus_message* m, void* /* userdata */,
                            sd_bus_error* retError)
{
    if (retError == nullptr || (sd_bus_error_is_set(retError) != 0))
    {
        BMCWEB_LOG_ERROR("Got sdbus error on match");
        return 0;
    }

    sdbusplus::message_t message(m);
    std::string iface;
    dbus::utility::DBusPropertiesMap changedProperties;

    message.read(iface, changedProperties);
    const std::string* hostname = nullptr;
    for (const auto& propertyPair : changedProperties)
    {
        if (propertyPair.first == "HostName")
        {
            hostname = std::get_if<std::string>(&propertyPair.second);
        }
    }
    if (hostname == nullptr)
    {
        return 0;
    }

    BMCWEB_LOG_DEBUG("Read hostname from signal: {}", *hostname);
    const std::string certFile = "/etc/ssl/certs/https/server.pem";

    X509* cert = ensuressl::loadCert(certFile);
    if (cert == nullptr)
    {
        BMCWEB_LOG_ERROR("Failed to read cert");
        return 0;
    }

    const int maxKeySize = 256;
    std::array<char, maxKeySize> cnBuffer{};

    int cnLength =
        X509_NAME_get_text_by_NID(X509_get_subject_name(cert), NID_commonName,
                                  cnBuffer.data(), cnBuffer.size());
    if (cnLength == -1)
    {
        BMCWEB_LOG_ERROR("Failed to read NID_commonName");
        X509_free(cert);
        return 0;
    }
    std::string_view cnValue(std::begin(cnBuffer),
                             static_cast<size_t>(cnLength));

    EVP_PKEY* pPubKey = X509_get_pubkey(cert);
    if (pPubKey == nullptr)
    {
        BMCWEB_LOG_ERROR("Failed to get public key");
        X509_free(cert);
        return 0;
    }
    int isSelfSigned = X509_verify(cert, pPubKey);
    EVP_PKEY_free(pPubKey);

    BMCWEB_LOG_DEBUG(
        "Current HTTPs Certificate Subject CN: {}, New HostName: {}, isSelfSigned: {}",
        cnValue, *hostname, isSelfSigned);

    ASN1_IA5STRING* asn1 = static_cast<ASN1_IA5STRING*>(
        X509_get_ext_d2i(cert, NID_netscape_comment, nullptr, nullptr));
    if (asn1 != nullptr)
    {
        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
        std::string_view comment(reinterpret_cast<const char*>(asn1->data),
                                 static_cast<size_t>(asn1->length));
        BMCWEB_LOG_DEBUG("x509Comment: {}", comment);

        if (ensuressl::x509Comment == comment && isSelfSigned == 1 &&
            cnValue != *hostname)
        {
            BMCWEB_LOG_INFO(
                "Ready to generate new HTTPs certificate with subject cn: {}",
                *hostname);

            std::string certData = ensuressl::generateSslCertificate(*hostname);
            if (certData.empty())
            {
                BMCWEB_LOG_ERROR("Failed to generate cert");
                return 0;
            }

            std::optional<std::filesystem::path> certPath =
                writeCertToUniqueTempFile(certData);
            if (!certPath)
            {
                return 0;
            }

            installCertificate(*certPath);
        }
        ASN1_STRING_free(asn1);
    }
    X509_free(cert);
    return 0;
}

inline void registerHostnameSignal()
{
    BMCWEB_LOG_INFO("Register HostName PropertiesChanged Signal");
    std::string propertiesMatchString =
        ("type='signal',"
         "interface='org.freedesktop.DBus.Properties',"
         "path='/xyz/openbmc_project/network/config',"
         "arg0='xyz.openbmc_project.Network.SystemConfiguration',"
         "member='PropertiesChanged'");

    hostnameSignalMonitor = std::make_unique<sdbusplus::bus::match_t>(
        *crow::connections::systemBus, propertiesMatchString, onPropertyUpdate,
        nullptr);
}
} // namespace hostname_monitor
} // namespace crow
