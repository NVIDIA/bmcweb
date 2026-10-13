/*
 * SPDX-FileCopyrightText: Copyright (c) 2021-2026 NVIDIA CORPORATION &
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

#include "bmcweb_config.h"

#include "nvidia_api_metrics.hpp"

#include <boost/url/url_view_base.hpp>

#include <string_view>

// Forward declaration for API Metrics
namespace redfish
{
namespace api_metrics
{
bool& getEnabled();
} // namespace api_metrics
} // namespace redfish

namespace nvidia
{
namespace http
{

/**
 * @brief Submit an API request to the non-blocking metrics socket
 *
 * @param clientIp Client IP address
 * @param method HTTP method
 * @param uri Request URI
 */
inline void logRedfishRequest(std::string_view clientIp,
                              std::string_view method,
                              const boost::urls::url_view_base& uri)
{
    if constexpr (BMCWEB_NVIDIA_API_METRICS)
    {
        // Check if API Metrics is enabled
        if (!redfish::api_metrics::getEnabled())
        {
            return;
        }

        api_metrics::submitApiMetrics(clientIp, method, uri.encoded_path());
    }
}

} // namespace http
} // namespace nvidia
