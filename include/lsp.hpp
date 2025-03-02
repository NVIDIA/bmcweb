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

#include <cstdio>
#include <cstring>
#include <vector>

namespace lsp
{

// IMPORTANT: If making any changes here, make sure to edit
// phosphor-certificate-manager also, or this might break certificate
// functionality. This is temporary until the real passphrase module is
// implemented.

inline std::vector<char>& getLsp()
{
    static std::vector<char> lspBuf = {
        0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0a,
        0x0b, 0x0c, 0x0d, 0x0e, 0x0f, 0x00, 0x01, 0x02, 0x03, 0x04, 0x05,
        0x06, 0x07, 0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f,
    };
    return lspBuf;
}

inline int passwordCallback(char* buf, int size, int /*unused*/,
                            void* /*unused*/)
{
    std::vector<char>& pwd = getLsp();
    size_t pwdsz = static_cast<unsigned int>(size) < pwd.size()
                       ? static_cast<size_t>(size)
                       : static_cast<size_t>(pwd.size());
    memcpy(buf, pwd.data(), pwdsz);
    return static_cast<int>(pwdsz);
}

// This is required to avoid passphrase prompts in certain cases when using
// openssl APIs
inline int emptyPasswordCallback(char* /*unused*/, int /*unused*/,
                                 int /*unused*/, void* /*unused*/)
{
    return 0;
}

} // namespace lsp
