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

#include <zlib.h>

#include <cstring>
#include <string>

inline bool gzipInflate(const std::string& compressedBytes,
                        std::string& uncompressedBytes)
{
    if (compressedBytes.empty())
    {
        uncompressedBytes = compressedBytes;
        return true;
    }

    uncompressedBytes.clear();

    unsigned half_length = compressedBytes.size() / 2;

    z_stream strm{};

    // The following line is nolint because we're declaring away constness.
    // It's not clear why the input buffers on zlib aren't const, so this is a
    // bit of a cheat for the moment
    strm.next_in = (Bytef*)compressedBytes.data(); // NOLINT
    strm.avail_in = compressedBytes.size();
    strm.total_out = 0;
    strm.zalloc = Z_NULL;
    strm.zfree = Z_NULL;

    bool done = false;

    if (inflateInit2(&strm, (16 + MAX_WBITS)) != Z_OK)
    {
        return false;
    }

    while (!done)
    {
        // If our output buffer is too small
        if (strm.total_out >= uncompressedBytes.size())
        {
            uncompressedBytes.resize(uncompressedBytes.size() + half_length);
        }

        strm.next_out =
            (Bytef*)(uncompressedBytes.data() + strm.total_out); // NOLINT
        strm.avail_out =
            ((uLong)uncompressedBytes.size() - strm.total_out); // NOLINT

        // Inflate another chunk.
        int err = inflate(&strm, Z_SYNC_FLUSH);
        if (err == Z_STREAM_END)
        {
            done = true;
        }
        else if (err != Z_OK)
        {
            break;
        }
    }

    return inflateEnd(&strm) == Z_OK;
}
