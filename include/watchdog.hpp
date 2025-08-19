<<<<<<< HEAD
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

#include "logging.hpp" // For BMCWEB_LOG_ERROR

#include <systemd/sd-daemon.h>

#include <boost/asio/io_context.hpp>
#include <boost/asio/steady_timer.hpp>

#include <chrono>
#include <functional> // For std::function

namespace crow
{

namespace watchdog
{

class ServiceWD
{
  public:
    ServiceWD(const int expiryTimeInSIn, boost::asio::io_context& io) :
        timer(io), expiryTimeInS(expiryTimeInSIn)
    {
        timer.expires_after(std::chrono::seconds(expiryTimeInSIn));
        handler = [this](const boost::system::error_code& error) {
            if (error)
            {
                BMCWEB_LOG_ERROR("ServiceWD async_wait failed: {}",
                                 error.message());
            }
            sd_notify(0, "WATCHDOG=1");
            timer.expires_after(std::chrono::seconds(this->expiryTimeInS));
            timer.async_wait(handler);
        };
        timer.async_wait(handler);
    }

  private:
    boost::asio::steady_timer timer;
    const int expiryTimeInS;
    std::function<void(const boost::system::error_code& error)> handler;
};

} // namespace watchdog
} // namespace crow
||||||| 80d2ef31c
=======
// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: Copyright OpenBMC Authors
#pragma once

#include "bmcweb_config.h"

#include "io_context_singleton.hpp"
#include "logging.hpp"

#include <systemd/sd-daemon.h>

#include <boost/asio/steady_timer.hpp>
#include <boost/system/error_code.hpp>

#include <chrono>
#include <cstdint>
#include <functional>
#include <ratio>

namespace bmcweb
{

class ServiceWatchdog
{
  public:
    ServiceWatchdog() : timer(getIoContext())
    {
        uint64_t usecondTimeout = 0;
        if (sd_watchdog_enabled(0, &usecondTimeout) <= 0)
        {
            if (BMCWEB_WATCHDOG_TIMEOUT_SECONDS > 0)
            {
                BMCWEB_LOG_WARNING(
                    "Watchdog timeout was enabled at compile time, but disabled at runtime");
            }
            return;
        }
        // Pet the watchdog N times faster than required.
        uint64_t petRatio = 4;
        watchdogTime = std::chrono::duration<uint64_t, std::micro>(
            usecondTimeout / petRatio);
        startTimer();
    }

  private:
    void startTimer()
    {
        timer.expires_after(watchdogTime);
        timer.async_wait(
            std::bind_front(&ServiceWatchdog::handleTimeout, this));
    }

    void handleTimeout(const boost::system::error_code& ec)
    {
        if (ec)
        {
            BMCWEB_LOG_ERROR("Watchdog timer async_wait failed: {}",
                             ec.message());
            return;
        }

        int rc = sd_notify(0, "WATCHDOG=1");
        if (rc < 0)
        {
            BMCWEB_LOG_ERROR("sd_notify failed: {}", -rc);
            return;
        }

        startTimer();
    }

    boost::asio::steady_timer timer;
    std::chrono::duration<uint64_t, std::micro> watchdogTime{};
};

} // namespace bmcweb
>>>>>>> origin/master
