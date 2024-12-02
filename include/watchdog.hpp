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
#include <systemd/sd-daemon.h>

#include <boost/asio/steady_timer.hpp>

namespace crow
{

namespace watchdog
{

class ServiceWD
{
  public:
    ServiceWD(const int expiryTimeInS,
              std::shared_ptr<boost::asio::io_context>& io) :
        timer(*io), expiryTimeInS(expiryTimeInS)
    {
        timer.expires_after(std::chrono::seconds(expiryTimeInS));
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
