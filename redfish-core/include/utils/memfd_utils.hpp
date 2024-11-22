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
#include "logging.hpp"

#include <sys/mman.h> // for memfd_create
#include <sys/stat.h> // for fstat
#include <unistd.h>   // for write and lseek

#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>

namespace redfish
{
struct MemoryFD
{
    int fd = -1;

    explicit MemoryFD(int fd) : fd(fd) {}
    explicit MemoryFD() : fd(memfd_create("bmcweb_memory_fd", 0))
    {
        if (fd == -1)
        {
            throw std::runtime_error("MemoryFD - memfd_create failed: " +
                                     std::string(strerror(errno)));
        }
    }

    MemoryFD(const MemoryFD&) = delete;
    MemoryFD(MemoryFD&& other) noexcept : fd(other.fd)
    {
        other.fd = -1;
    }
    MemoryFD& operator=(const MemoryFD&) = delete;
    MemoryFD& operator=(MemoryFD&&) = delete;

    ~MemoryFD()
    {
        if (fd != -1)
        {
            close(fd);
        }
    }

    bool rewind() const
    {
        if (lseek(fd, 0, SEEK_SET) == -1)
        {
            BMCWEB_LOG_ERROR("Failed to seek to beginning of image memfd");
            return false;
        }
        return true;
    }

    void write(const std::vector<uint8_t>& data) const
    {
        if (!rewind())
        {
            throw std::runtime_error("MemoryFD - lseek failed: " +
                                     std::string(strerror(errno)));
        }
        ssize_t bytesWritten = ::write(fd, data.data(), data.size());
        if (bytesWritten < 0)
        {
            throw std::runtime_error("MemoryFD - write failed: " +
                                     std::string(strerror(errno)));
        }
        else if (static_cast<size_t>(bytesWritten) != data.size())
        {
            throw std::runtime_error(
                "MemoryFD - Fewer bytes written than expected");
        }
    }
    void read(std::vector<uint8_t>& data) const
    {
        if (!rewind())
        {
            throw std::runtime_error("MemoryFD - lseek failed: " +
                                     std::string(strerror(errno)));
        }
        struct stat fileStat;
        if (fstat(fd, &fileStat) < 0)
        {
            throw std::runtime_error("MemoryFD - fstat failed: " +
                                     std::string(strerror(errno)));
        }
        if (fileStat.st_size < 0)
        {
            throw std::runtime_error("MemoryFD - Invalid file size in fd");
        }
        data.resize(static_cast<size_t>(fileStat.st_size));
        ssize_t bytesRead = ::read(fd, data.data(), data.size());
        if (bytesRead < 0)
        {
            throw std::runtime_error("MemoryFD - read failed: " +
                                     std::string(strerror(errno)));
        }
        else if (static_cast<size_t>(bytesRead) != data.size())
        {
            throw std::runtime_error(
                "MemoryFD - Fewer bytes read than expected");
        }
    }
};

} // namespace redfish
