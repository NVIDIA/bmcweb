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

extern "C"
{
#include <openssl/bio.h>
#include <openssl/evp.h>
}

#include <string>

namespace ensuressl
{

std::string
    ensureOpensslKeyPresentEncryptedAndValid(const std::string& filepath);

void encryptCredentials(const std::string& filename);

EVP_PKEY* readBioEncryptedPrivateKey(BIO* out);

int writeBioEncryptedPrivateKey(BIO* out, const EVP_PKEY* x);

} // namespace ensuressl
