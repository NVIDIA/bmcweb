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

#include "debug_token/request_utils.hpp"
#include "debug_token/status_utils.hpp"
#include "nlohmann/json.hpp"
#include "utils/mctp_utils.hpp"

#include <vector>

namespace redfish::debug_token
{

enum class EndpointState
{
    None,
    StatusAcquired,
    TokenInstalled,
    RequestAcquired,
    Error,
    DebugTokenUnsupported
};

enum class EndpointType
{
    NSM,
    SPDM
};

class DebugTokenEndpoint
{
  public:
    DebugTokenEndpoint(const DebugTokenEndpoint&) = delete;
    DebugTokenEndpoint(DebugTokenEndpoint&&) = delete;
    DebugTokenEndpoint& operator=(const DebugTokenEndpoint&) = delete;
    DebugTokenEndpoint& operator=(const DebugTokenEndpoint&&) = delete;

    virtual ~DebugTokenEndpoint() = default;

    virtual int getMctpEid() const = 0;

    virtual const std::string getObject() const = 0;

    const std::vector<uint8_t>& getRequest() const
    {
        return request;
    }
    virtual void setRequest(std::vector<uint8_t>&) = 0;

    virtual void getStatusAsJson(nlohmann::json&) const = 0;

    virtual EndpointType getType() const = 0;

    EndpointState getState() const
    {
        return state;
    }

    void setError()
    {
        state = EndpointState::Error;
    }

  protected:
    DebugTokenEndpoint() = default;

    EndpointState state{EndpointState::None};

    std::vector<uint8_t> request;
};

class DebugTokenNsmEndpoint : public DebugTokenEndpoint
{
  public:
    DebugTokenNsmEndpoint(std::string nsmObjectPath) : objectPath(nsmObjectPath)
    {}
    DebugTokenNsmEndpoint() = delete;
    DebugTokenNsmEndpoint(const DebugTokenNsmEndpoint&) = delete;
    DebugTokenNsmEndpoint(DebugTokenNsmEndpoint&&) = delete;
    DebugTokenNsmEndpoint& operator=(const DebugTokenNsmEndpoint&) = delete;
    DebugTokenNsmEndpoint& operator=(const DebugTokenNsmEndpoint&&) = delete;

    int getMctpEid() const override
    {
        return -1;
    }

    const std::string getObject() const override
    {
        return objectPath;
    }

    void setRequest(std::vector<uint8_t>& r) override
    {
        NsmDebugTokenRequest* nsmReq =
            reinterpret_cast<NsmDebugTokenRequest*>(r.data());
        switch (nsmReq->status)
        {
            case NsmDebugTokenChallengeQueryStatus::OK:
                state = EndpointState::RequestAcquired;
                request = addTokenRequestHeader(
                    convertNsmTokenRequestToSpdmTranscript(r));
                return;
            case NsmDebugTokenChallengeQueryStatus::TokenAlreadyApplied:
                state = EndpointState::TokenInstalled;
                return;
            case NsmDebugTokenChallengeQueryStatus::TokenNotSupported:
                state = EndpointState::DebugTokenUnsupported;
                return;
            default:
                BMCWEB_LOG_ERROR("NSM token request - object: {} status: {}",
                                 objectPath, nsmReq->status);
                state = EndpointState::Error;
                return;
        }
    }

    void getStatusAsJson(nlohmann::json& json) const override
    {
        if (status)
        {
            nsmTokenStatusToJson(*status, json);
        }
    }

    void setStatus(std::unique_ptr<NsmTokenStatus> s)
    {
        status = std::move(s);
        if (status->tokenStatus == "DebugSessionActive")
        {
            state = EndpointState::TokenInstalled;
            return;
        }
        if (status->tokenStatus == "OperationFailure")
        {
            state = EndpointState::Error;
            return;
        }
        state = EndpointState::StatusAcquired;
    }

    void setStatus(EndpointState s)
    {
        state = s;
    }

    EndpointType getType() const override
    {
        return EndpointType::NSM;
    }

  private:
    std::string objectPath;
    std::unique_ptr<NsmTokenStatus> status;
};

class DebugTokenSpdmEndpoint : public DebugTokenEndpoint
{
  public:
    DebugTokenSpdmEndpoint(mctp_utils::MctpEndpoint& mctpEndpoint) :
        mctpEp(std::move(mctpEndpoint))
    {}
    DebugTokenSpdmEndpoint() = delete;
    DebugTokenSpdmEndpoint(const DebugTokenSpdmEndpoint&) = delete;
    DebugTokenSpdmEndpoint(DebugTokenSpdmEndpoint&&) = delete;
    DebugTokenSpdmEndpoint& operator=(const DebugTokenSpdmEndpoint&) = delete;
    DebugTokenSpdmEndpoint& operator=(const DebugTokenSpdmEndpoint&&) = delete;

    int getMctpEid() const override
    {
        return mctpEp.getMctpEid();
    }

    const std::string getObject() const override
    {
        return mctpEp.getSpdmObject();
    }

    void setRequest(std::vector<uint8_t>& r) override
    {
        state = EndpointState::RequestAcquired;
        request = addTokenRequestHeader(r);
    }

    void getStatusAsJson(nlohmann::json& json) const override
    {
        if (status)
        {
            vdmTokenStatusToJson(*status, json);
        }
    }

    void setStatus(std::unique_ptr<VdmTokenStatus> s)
    {
        status = std::move(s);
        if (status->responseStatus == VdmResponseStatus::INVALID_LENGTH ||
            status->responseStatus == VdmResponseStatus::PROCESSING_ERROR ||
            status->responseStatus == VdmResponseStatus::ERROR ||
            status->tokenStatus == VdmTokenInstallationStatus::INVALID)
        {
            state = EndpointState::Error;
            return;
        }
        if (status->responseStatus == VdmResponseStatus::NOT_SUPPORTED)
        {
            state = EndpointState::DebugTokenUnsupported;
            return;
        }
        if (status->tokenStatus == VdmTokenInstallationStatus::NOT_INSTALLED)
        {
            state = EndpointState::StatusAcquired;
            return;
        }
        if (status->tokenStatus == VdmTokenInstallationStatus::INSTALLED)
        {
            state = EndpointState::TokenInstalled;
            return;
        }
    }

    EndpointType getType() const override
    {
        return EndpointType::SPDM;
    }

  private:
    mctp_utils::MctpEndpoint mctpEp;
    std::unique_ptr<VdmTokenStatus> status;
};

} // namespace redfish::debug_token
