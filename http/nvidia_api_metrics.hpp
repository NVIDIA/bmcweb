/*
 * SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION &
 * AFFILIATES. All rights reserved.
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#include <string_view>

namespace nvidia::http::api_metrics
{

void submitApiMetrics(std::string_view clientIp, std::string_view method,
                      std::string_view uri) noexcept;

} // namespace nvidia::http::api_metrics
