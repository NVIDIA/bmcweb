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
#include <vector>

extern "C"
{
#include <openssl/pem.h>
#include <openssl/x509.h>
}

#include "asn1.hpp"
#include "logging.hpp"
#include "lsp.hpp"
#include "nvidia_ssl_key_handler.hpp"
#include "ssl_key_handler.hpp"

namespace ensuressl
{

void encryptCredentials(const std::string& filename)
{
    std::vector<char>& pwd = lsp::getLsp();
    auto fp = fopen(filename.c_str(), "r");
    if (fp == nullptr)
    {
        BMCWEB_LOG_ERROR("Cannot open filename for reading: {}", filename);
        return;
    }
    auto pkey = PEM_read_PrivateKey(fp, nullptr, lsp::emptyPasswordCallback,
                                    nullptr);
    if (pkey == nullptr)
    {
        BMCWEB_LOG_ERROR("Could not read private key from file: {}", filename);
        return;
    }
    fseek(fp, 0, SEEK_SET);
    auto x509 = PEM_read_X509(fp, nullptr, nullptr, nullptr);
    fclose(fp);

    fp = fopen(filename.c_str(), "w");
    if (fp == nullptr)
    {
        BMCWEB_LOG_ERROR("Cannot open filename for writing: {}", filename);
        return;
    }
    PEM_write_PrivateKey(fp, pkey, EVP_aes_256_cbc(),
                         reinterpret_cast<const unsigned char*>(pwd.data()),
                         static_cast<int>(pwd.size()), nullptr, nullptr);
    if (x509 != nullptr)
    {
        BMCWEB_LOG_INFO("Writing x509 cert.");
        PEM_write_X509(fp, x509);
    }
    fclose(fp);

    BMCWEB_LOG_INFO("Encrypted {}", filename);

    EVP_PKEY_free(pkey);
    X509_free(x509);
}

std::string
    ensureOpensslKeyPresentEncryptedAndValid(const std::string& filepath)
{
    std::string cert;
    bool pkeyIsEncrypted = false;

    auto ret = asn1::pemPkeyIsEncrypted(filepath.c_str(), &pkeyIsEncrypted);
    if (ret == -1)
    {
        BMCWEB_LOG_INFO("No private key file available.");
    }
    else if (ret < -1)
    {
        BMCWEB_LOG_ERROR(
            "Error while determining if private key is encrypted.");
    }
    else if (!pkeyIsEncrypted)
    {
        BMCWEB_LOG_INFO("Encrypting private key in file: {}", filepath);
        encryptCredentials(filepath);
    }
    else if (pkeyIsEncrypted)
    {
        BMCWEB_LOG_INFO("TLS key is encrypted.");
    }

    cert = verifyOpensslKeyCert(filepath);

    if (cert.empty())
    {
        BMCWEB_LOG_WARNING("Error in verifying signature, regenerating");
        cert = generateSslCertificate("testhost");
        if (cert.empty())
        {
            BMCWEB_LOG_ERROR("Failed to generate cert");
        }
        else
        {
            writeCertificateToFile(filepath, cert);
        }
    }
    return cert;
}

EVP_PKEY* readBioEncryptedPrivateKey(BIO* out)
{
    return PEM_read_bio_PrivateKey(out, nullptr, lsp::passwordCallback,
                                   nullptr);
}

int writeBioEncryptedPrivateKey(BIO* out, const EVP_PKEY* x)
{
    std::vector<char>& pwd = lsp::getLsp();
    return PEM_write_bio_PrivateKey(
        out, x, EVP_aes_256_cbc(),
        reinterpret_cast<const unsigned char*>(pwd.data()),
        static_cast<int>(pwd.size()), nullptr, nullptr);
}

} // namespace ensuressl
