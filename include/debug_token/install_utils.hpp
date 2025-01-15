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

#include <map>
#include <string>

namespace redfish::debug_token
{

enum VdmDebugTokenInstallErrorCode
{
    Success = 0,
    TokenVersionInvalid = 1,
    TokenAuthenticationFailed = 2,
    TokenNonceInvalid = 3,
    TokenSerialNumberInvalid = 4,
    TokenECFWVersionInvalid = 5,
    DisableBackgroundCopyCheckFailed = 6,
    InternalError = 7
};

inline std::string getVdmDebugTokenInstallErrorDescription(int code)
{
    static std::map<VdmDebugTokenInstallErrorCode, std::string>
        debugTokenInstallErrorMap{
            {VdmDebugTokenInstallErrorCode::Success, "Success"},
            {VdmDebugTokenInstallErrorCode::TokenVersionInvalid,
             "Token version invalid"},
            {VdmDebugTokenInstallErrorCode::TokenAuthenticationFailed,
             "Token authentication failed"},
            {VdmDebugTokenInstallErrorCode::TokenNonceInvalid,
             "Token nonce invalid"},
            {VdmDebugTokenInstallErrorCode::TokenSerialNumberInvalid,
             "Token serial number invalid"},
            {VdmDebugTokenInstallErrorCode::TokenECFWVersionInvalid,
             "Token ECFW version invalid"},
            {VdmDebugTokenInstallErrorCode::DisableBackgroundCopyCheckFailed,
             "Disable background copy check failed"},
            {VdmDebugTokenInstallErrorCode::InternalError, "Internal error"},
        };
    try
    {
        return debugTokenInstallErrorMap.at(
            static_cast<VdmDebugTokenInstallErrorCode>(code));
    }
    catch (const std::exception&)
    {
        return "Invalid error code";
    }
}

} // namespace redfish::debug_token
