/*
 * SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION &
 * AFFILIATES. All rights reserved.
 * SPDX-License-Identifier: Apache-2.0
 */

#include "nvidia_api_metrics.hpp"

#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include <array>
#include <cstddef>
#include <cstring>
#include <span>
#include <string_view>

namespace nvidia::http::api_metrics
{
namespace
{

constexpr std::string_view truncationMarker = " [truncated]";
constexpr std::string_view socketPath = "/run/bmc-cmd-metrics.sock";
constexpr size_t maxRecordSize = 4096;
constexpr std::string_view protocolHeader =
    "<14>1 - - bmc-cmd-metrics - CMDMETRICS-V1 - ";
constexpr std::string_view apiMetricsMessagePrefix = "API Metrics: ";
constexpr socklen_t socketAddressLength = static_cast<socklen_t>(
    offsetof(sockaddr_un, sun_path) + socketPath.size() + 1U);
static_assert(truncationMarker.size() <= maxRecordSize);

class RecordBuilder
{
  public:
    void append(std::string_view value) noexcept
    {
        for (char ch : value)
        {
            if (size >= data.size())
            {
                truncated = true;
                return;
            }
            data[size++] = (ch == '\n' || ch == '\r' || ch == '\0') ? ' ' : ch;
        }
    }

    template <typename... Parts>
    void appendParts(const Parts&... parts) noexcept
    {
        (append(std::string_view{parts}), ...);
    }

    std::span<const char> finish() noexcept
    {
        if (truncated)
        {
            const size_t markerOffset = data.size() - truncationMarker.size();
            std::memcpy(data.data() + markerOffset, truncationMarker.data(),
                        truncationMarker.size());
        }
        return {data.data(), size};
    }

  private:
    std::array<char, maxRecordSize> data{};
    size_t size = 0;
    bool truncated = false;
};

class SocketSender
{
  public:
    SocketSender() noexcept
    {
        static_assert(socketPath.size() < sizeof(address.sun_path));
        address.sun_family = AF_UNIX;
        std::memcpy(address.sun_path, socketPath.data(), socketPath.size());
        address.sun_path[socketPath.size()] = '\0';

        fd = socket(AF_UNIX, SOCK_DGRAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
    }

    ~SocketSender()
    {
        if (fd >= 0)
        {
            close(fd);
        }
    }

    SocketSender(const SocketSender&) = delete;
    SocketSender& operator=(const SocketSender&) = delete;

    void send(std::span<const char> record) const noexcept
    {
        if (fd < 0)
        {
            return;
        }

        static_cast<void>(sendto(
            fd, record.data(), record.size(), MSG_DONTWAIT | MSG_NOSIGNAL,
            reinterpret_cast<const sockaddr*>(&address), socketAddressLength));
    }

  private:
    int fd = -1;
    sockaddr_un address{};
};

SocketSender& getSender() noexcept
{
    static SocketSender sender;
    return sender;
}

} // namespace

void submitApiMetrics(std::string_view clientIp, std::string_view method,
                      std::string_view uri) noexcept
{
    RecordBuilder record;
    record.appendParts(protocolHeader, apiMetricsMessagePrefix, "IP=", clientIp,
                       " METHOD=", method, " URI=", uri);
    getSender().send(record.finish());
}

} // namespace nvidia::http::api_metrics
