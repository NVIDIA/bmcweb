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

#include <string_view>

namespace redfish::dot
{

// D-Bus interface name for DOT action operations
constexpr const std::string_view dotActionIntf = "com.nvidia.Dot.Action";

// D-Bus interface name for async status monitoring
constexpr const std::string_view asyncStatusIntf = "com.nvidia.Async.Status";

// D-Bus interface name for async value/result data
constexpr const std::string_view asyncValueIntf = "com.nvidia.Async.Value";

// Base D-Bus path for async operation objects
constexpr const std::string_view asyncOperationBasePath =
    "/com/nvidia/nsmd/AsyncOperation";

constexpr const std::string_view asyncStatusProperty = "Status";

constexpr const std::string_view asyncValueProperty = "Value";
} // namespace redfish::dot
