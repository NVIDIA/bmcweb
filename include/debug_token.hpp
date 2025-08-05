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

#include "component_integrity.hpp"
#include "dbus_utility.hpp"
#include "debug_token/base.hpp"
#include "debug_token/endpoint.hpp"
#include "debug_token/nsm_async_aggregate.hpp"
#include "debug_token/nsm_status_utils.hpp"
#include "debug_token/request_utils.hpp"
#include "debug_token/vdm_status.hpp"
#include "debug_token/vdm_status_utils.hpp"
#include "nvidia_cpu_debug_token.hpp"
#include "openbmc_dbus_rest.hpp"
#include "utils/dbus_utils.hpp"
#include "utils/mctp_utils.hpp"

#include <sys/stat.h>
#include <unistd.h>

#include <boost/process.hpp>
#include <boost/process/async.hpp>
#include <boost/process/child.hpp>

#include <functional>
#include <memory>
#include <sstream>
#include <vector>

namespace redfish
{
namespace debug_token
{
// mctp-vdm-util's output size per endpoint
constexpr const size_t statusQueryOutputSize = 256;
constexpr const int statusQueryTimeoutSeconds = 60;

constexpr const std::string_view nsmDebugTokenSpecifier = "CRDT";

constexpr const auto spdmMatchRule =
    "type='signal',interface='org.freedesktop.DBus.Properties',"
    "member='PropertiesChanged',"
    "path_namespace='/xyz/openbmc_project/SPDM'";

enum class RequestType
{
    DebugTokenRequest,
    DOTCAKUnlockTokenRequest,
    DOTEnableTokenRequest,
    DOTSignTestToken,
    DOTOverrideTokenRequest
};

using ResultCallback = std::function<void(
    const std::shared_ptr<std::vector<std::unique_ptr<DebugTokenEndpoint>>>&)>;
using ErrorCallback = std::function<void(
    bool /* is critical (end of operation) */,
    const std::string& /* resource / procedure associated with the error */,
    const std::string& /* error message */)>;

class OperationHandler
{
  public:
    OperationHandler(const OperationHandler&) = delete;
    OperationHandler(OperationHandler&&) = delete;
    OperationHandler& operator=(const OperationHandler&) = delete;
    OperationHandler& operator=(const OperationHandler&&) = delete;

    virtual ~OperationHandler() = default;

    virtual void getResult(std::string&) const = 0;

  protected:
    OperationHandler() = default;

    std::shared_ptr<std::vector<std::unique_ptr<DebugTokenEndpoint>>> endpoints;

    ResultCallback resCallback;
    ErrorCallback errCallback;

    std::unique_ptr<sdbusplus::bus::match_t> spdmMatch;

    void createSpdmMatch(
        const std::function<void(const std::string&, const std::string&)>&
            callback)
    {
        spdmMatch = std::make_unique<sdbusplus::bus::match_t>(
            *crow::connections::systemBus, spdmMatchRule,
            [callback](sdbusplus::message_t& msg) {
                if (msg.is_method_error())
                {
                    BMCWEB_LOG_ERROR("SPDM match message error");
                    return;
                }
                std::string object(msg.get_path());
                BMCWEB_LOG_DEBUG("SPDM match handler: {}", object);
                std::string interface;
                std::map<std::string, dbus::utility::DbusVariantType> props;
                msg.read(interface, props);
                std::string* spdmStatus = nullptr;
                if (interface == spdmResponderIntf)
                {
                    auto it = props.find("Status");
                    if (it != props.end())
                    {
                        spdmStatus = std::get_if<std::string>(&(it->second));
                    }
                }
                if (spdmStatus == nullptr)
                {
                    return;
                }
                std::string status =
                    spdmStatus->substr(spdmStatus->find_last_of('.') + 1);
                callback(object, status);
            });
    }

    void resetMatches()
    {
        spdmMatch.reset();
    }
};

class StatusQueryHandler : public OperationHandler
{
  public:
    StatusQueryHandler(ResultCallback&& resultCallback,
                       ErrorCallback&& errorCallback,
                       const std::vector<std::string>& endpointFilter =
                           std::vector<std::string>(),
                       bool useNsm = true)
    {
        BMCWEB_LOG_DEBUG("StatusQueryHandler constructor");
        resCallback = resultCallback;
        errCallback = errorCallback;
        getCpuObjectPath([this, errorCallback,
                          endpointFilter](const boost::system::error_code&,
                                          const std::string& cpuPath) {
            mctp_utils::enumerateMctpEndpoints(
                [this, cpuPath](
                    const std::shared_ptr<
                        std::vector<mctp_utils::MctpEndpoint>>& mctpEndpoints) {
                    const std::string desc = "SPDM endpoint enumeration";
                    BMCWEB_LOG_DEBUG("{}", desc);
                    if (!mctpEndpoints || mctpEndpoints->empty())
                    {
                        spdmPending = false;
                        BMCWEB_LOG_ERROR("{}: {}", desc, "no endpoints found");
                        finalize();
                        return;
                    }
                    if (!endpoints)
                    {
                        endpoints = std::make_shared<
                            std::vector<std::unique_ptr<DebugTokenEndpoint>>>();
                        endpoints->reserve(mctpEndpoints->size());
                    }
                    for (auto& endpoint : *mctpEndpoints)
                    {
                        if (!endpoint.isEnabled())
                        {
                            continue;
                        }
                        // ignore satmc (CPU debug token) endpoint
                        if (endpoint.getSpdmObject() == cpuPath)
                        {
                            continue;
                        }
                        const auto& msgTypes = endpoint.getMctpMessageTypes();
                        if (std::find(msgTypes.begin(), msgTypes.end(),
                                      mctp_utils::mctpMessageTypeVdm) !=
                            msgTypes.end())
                        {
                            endpoints->emplace_back(
                                std::make_unique<DebugTokenSpdmEndpoint>(
                                    endpoint));
                        }
                    }
                    endpoints->shrink_to_fit();
                    getVdmStatus();
                    finalize();
                },
                [this](bool, const std::string& desc,
                       const std::string& error) {
                    spdmPending = false;
                    errCallback(false, desc, error);
                    finalize();
                },
                endpointFilter,
                static_cast<uint64_t>(statusQueryTimeoutSeconds) * 1000000U);
        });
        if (useNsm)
        {
            getNsmStatus();
        }
        else
        {
            nsmPending = false;
        }
    }

    StatusQueryHandler() = delete;
    StatusQueryHandler(const StatusQueryHandler&) = delete;
    StatusQueryHandler(StatusQueryHandler&&) = delete;
    StatusQueryHandler& operator=(const StatusQueryHandler&) = delete;
    StatusQueryHandler& operator=(const StatusQueryHandler&&) = delete;

    ~StatusQueryHandler() override = default;

    void getResult(std::string& result) const override
    {
        if (endpoints)
        {
            nlohmann::json statusOutput;
            auto statusArray = nlohmann::json::array();
            for (const auto& ep : *endpoints)
            {
                auto state = ep->getState();
                if (state != EndpointState::StatusAcquired &&
                    state != EndpointState::TokenInstalled)
                {
                    continue;
                }
                nlohmann::json epOutput;
                std::filesystem::path path(ep->getObject());
                epOutput["@odata.id"] = std::string("/redfish/v1/Chassis/") +
                                        std::string(path.filename());
                ep->getStatusAsJson(epOutput);
                statusArray.push_back(std::move(epOutput));
            }
            statusOutput["DebugTokenStatus"] = std::move(statusArray);
            result = statusOutput.dump(4);
        }
    }

  private:
    bool nsmPending{true};
    bool spdmPending{true};

    void getNsmStatus()
    {
        nsm_async::aggregate::Handler::startOperation(
            nsm_async::aggregate::Operation::GetTokenStatus,
            std::string(nsmDebugTokenSpecifier),
            [this](const std::vector<nsm_async::aggregate::Result>& results) {
                const std::string desc = "NSM token status acquisition";
                BMCWEB_LOG_DEBUG("{}", desc);
                if (results.empty())
                {
                    errCallback(false, desc,
                                "No valid NSM token status responses");
                }
                if (!endpoints)
                {
                    endpoints = std::make_shared<
                        std::vector<std::unique_ptr<DebugTokenEndpoint>>>();
                    endpoints->reserve(results.size());
                }
                else
                {
                    endpoints->reserve(endpoints->size() + results.size());
                }
                for (const auto& result : results)
                {
                    const auto& [object, state, output] = result;
                    endpoints->emplace_back(
                        std::make_unique<DebugTokenNsmEndpoint>(object));
                    DebugTokenNsmEndpoint* nsmEp =
                        dynamic_cast<DebugTokenNsmEndpoint*>(
                            endpoints->back().get());
                    if (state == EndpointState::StatusAcquired)
                    {
                        nsmEp->setStatus(std::get<NsmTokenStatus>(output));
                    }
                    else
                    {
                        nsmEp->setStatus(state);
                    }
                }
                endpoints->shrink_to_fit();
                nsmPending = false;
                finalize();
            });
    }

    void vdmStatusHandler(const std::vector<vdm_status::Result>& results)
    {
        const std::string desc = "VDM token status acquisition";
        BMCWEB_LOG_DEBUG("{}", desc);
        if (results.empty())
        {
            errCallback(false, desc, "no results");
            finalize();
            return;
        }
        for (const auto& endpoint : *endpoints)
        {
            if (endpoint->getType() != EndpointType::SPDM)
            {
                continue;
            }
            auto* spdmEp =
                dynamic_cast<DebugTokenSpdmEndpoint*>(endpoint.get());
            auto epEid = spdmEp->getMctpEid();
            if (epEid == -1)
            {
                continue;
            }
            auto result = std::find_if(
                results.begin(), results.end(),
                [epEid](const auto& res) { return std::get<0>(res) == epEid; });
            if (result == results.end())
            {
                errCallback(false, desc, "no data for " + spdmEp->getObject());
                spdmEp->setError();
                continue;
            }
            const auto& [eid, state, output] = *result;
            if (std::holds_alternative<VdmTokenStatus>(output))
            {
                spdmEp->setStatus(std::get<VdmTokenStatus>(output));
            }
            else
            {
                spdmEp->setStatus(state);
            }
        }
        spdmPending = false;
        finalize();
    }

    void getVdmStatus()
    {
        std::vector<vdm_status::Eid> eids;
        for (const auto& ep : *endpoints)
        {
            if (ep->getType() != EndpointType::SPDM)
            {
                continue;
            }
            auto mctpEid = ep->getMctpEid();
            if (mctpEid != -1)
            {
                eids.emplace_back(static_cast<vdm_status::Eid>(mctpEid));
            }
        }
        if (eids.empty())
        {
            errCallback(false, "VDM token status acquisition",
                        "no valid endpoints");
            spdmPending = false;
            finalize();
            return;
        }
        vdm_status::Handler::startOperation(
            eids, std::bind_front(&StatusQueryHandler::vdmStatusHandler, this));
    }

    void finalize()
    {
        const std::string desc = "Token status query processing";
        BMCWEB_LOG_DEBUG("{}", desc);
        if (nsmPending || spdmPending)
        {
            return;
        }
        if (!endpoints || endpoints->empty())
        {
            errCallback(true, desc, "No valid debug token status responses");
            return;
        }

        int completedRequestsCount = 0;
        for (const auto& ep : *endpoints)
        {
            auto state = ep->getState();
            if (state == EndpointState::None)
            {
                return;
            }
            ++completedRequestsCount;
        }
        resetMatches();
        if (completedRequestsCount > 0)
        {
            resCallback(endpoints);
            return;
        }
        errCallback(true, desc, "No valid debug token status responses");
    }
};

class RequestHandler : public OperationHandler
{
  public:
    RequestHandler(ResultCallback&& resultCallback,
                   ErrorCallback&& errorCallback, RequestType reqType,
                   const std::vector<std::string>& endpointFilter =
                       std::vector<std::string>()) : type(reqType)
    {
        BMCWEB_LOG_DEBUG("RequestHandler constructor");
        resCallback = resultCallback;
        errCallback = errorCallback;
        statusHandler = std::make_unique<StatusQueryHandler>(
            [this](const std::shared_ptr<
                   std::vector<std::unique_ptr<DebugTokenEndpoint>>>& epList) {
                if (!epList || epList->empty())
                {
                    errCallback(true, "Debug token status check",
                                "No valid endpoints");
                    return;
                }
                this->endpoints = epList;
                if (this->type == RequestType::DebugTokenRequest)
                {
                    getNsmRequest();
                }
                else
                {
                    nsmPending = false;
                }
                getSpdmRequest();
            },
            [this](bool critical, const std::string& desc,
                   const std::string& error) {
                errCallback(critical, desc, error);
            },
            endpointFilter, this->type == RequestType::DebugTokenRequest);
    }

    RequestHandler() = delete;
    RequestHandler(const RequestHandler&) = delete;
    RequestHandler(RequestHandler&&) = delete;
    RequestHandler& operator=(const RequestHandler&) = delete;
    RequestHandler& operator=(const RequestHandler&&) = delete;

    ~RequestHandler() override = default;

    void getResult(std::string& result) const override
    {
        if (!endpoints)
        {
            return;
        }
        std::vector<std::vector<uint8_t>> requests;
        for (const auto& ep : *endpoints)
        {
            if (ep->getState() == EndpointState::RequestAcquired)
            {
                requests.emplace_back(ep->getRequest());
            }
        }
        auto file = generateTokenRequestFile(requests);
        result = std::string(file.begin(), file.end());
    }

  private:
    std::unique_ptr<StatusQueryHandler> statusHandler;

    RequestType type;

    bool nsmPending{true};

    uint8_t typeToMeasurementIndex(RequestType reqType)
    {
        static const std::map<RequestType, uint8_t> indexMap{
            {RequestType::DebugTokenRequest, 50},
            {RequestType::DOTCAKUnlockTokenRequest, 58},
            {RequestType::DOTEnableTokenRequest, 59},
            {RequestType::DOTSignTestToken, 60},
            {RequestType::DOTOverrideTokenRequest, 61}};

        return indexMap.at(reqType);
    }

    bool isEndpointRequestPending(EndpointState state)
    {
        return state == EndpointState::StatusAcquired ||
               state == EndpointState::TokenInstalled;
    }

    void getNsmRequest()
    {
        nsm_async::aggregate::Handler::startOperation(
            nsm_async::aggregate::Operation::GenerateTokenRequest,
            std::string(nsmDebugTokenSpecifier),
            [this](const std::vector<nsm_async::aggregate::Result>& results) {
                const std::string desc = "NSM token request acquisition";
                BMCWEB_LOG_DEBUG("{}", desc);
                if (results.empty())
                {
                    errCallback(false, desc,
                                "No valid NSM token request responses");
                }
                for (const auto& result : results)
                {
                    const auto& [object, state, output] = result;
                    auto endpoint = std::find_if(
                        endpoints->begin(), endpoints->end(),
                        [obj = object](const auto& ep) {
                            if (ep == nullptr)
                            {
                                return false;
                            }
                            return ep->getType() == EndpointType::NSM &&
                                   ep->getObject() == obj;
                        });
                    if (endpoint == endpoints->end())
                    {
                        errCallback(false, desc, "unknown object");
                        return;
                    }
                    auto& ep = *endpoint;
                    DebugTokenNsmEndpoint* nsmEp =
                        dynamic_cast<DebugTokenNsmEndpoint*>(ep.get());
                    if (state == EndpointState::RequestAcquired)
                    {
                        nsmEp->setRequest(
                            std::get<std::vector<uint8_t>>(output));
                    }
                    else
                    {
                        nsmEp->setStatus(state);
                    }
                }
                nsmPending = false;
                endpoints->shrink_to_fit();
                finalize();
            });
    }

    void getSpdmRequest()
    {
        createSpdmMatch(
            [this](const std::string& object, const std::string& status) {
                spdmUpdate(object, status);
            });
        std::vector<uint8_t> indices{typeToMeasurementIndex(this->type)};
        bool refreshIssued = false;
        for (auto& ep : *endpoints)
        {
            auto epType = ep->getType();
            auto state = ep->getState();
            if (epType != EndpointType::SPDM ||
                !isEndpointRequestPending(state))
            {
                continue;
            }
            DebugTokenSpdmEndpoint* spdmEp =
                dynamic_cast<DebugTokenSpdmEndpoint*>(ep.get());
            auto objectPath = ep->getObject();
            const std::string statusDescStr =
                "SPDM refresh call for " + objectPath;
            BMCWEB_LOG_DEBUG("{}", statusDescStr);
            crow::connections::systemBus->async_method_call(
                [this, statusDescStr,
                 spdmEp](const boost::system::error_code& ec) {
                    if (ec)
                    {
                        BMCWEB_LOG_ERROR("{}: {}", statusDescStr, ec.message());
                        errCallback(false, statusDescStr, ec.message());
                        spdmEp->setError();
                        finalize();
                    }
                },
                spdmBusName, objectPath, spdmResponderIntf, "Refresh",
                static_cast<uint8_t>(0), std::vector<uint8_t>(), indices,
                static_cast<uint32_t>(0));
            refreshIssued = true;
        }
        if (!refreshIssued)
        {
            spdmMatch.reset();
            finalize();
        }
    }

    void spdmUpdate(const std::string& object, const std::string& status)
    {
        const std::string statusDescStr =
            "Update of " + object + " object with status " + status;
        BMCWEB_LOG_DEBUG("{}", statusDescStr);
        auto endpoint =
            std::find_if(endpoints->begin(), endpoints->end(),
                         [obj = object](const auto& ep) {
                             if (ep == nullptr)
                             {
                                 return false;
                             }
                             return ep->getType() == EndpointType::SPDM &&
                                    ep->getObject() == obj;
                         });
        if (endpoint == endpoints->end())
        {
            errCallback(false, statusDescStr, "unknown object");
            return;
        }
        auto& ep = *endpoint;
        auto state = ep->getState();
        if (state == EndpointState::Error ||
            state == EndpointState::RequestAcquired)
        {
            errCallback(false, statusDescStr, "received unexpected update");
        }
        else if (status == "Success")
        {
            DebugTokenSpdmEndpoint* spdmEp =
                dynamic_cast<DebugTokenSpdmEndpoint*>(ep.get());
            crow::connections::systemBus->async_method_call(
                [this, spdmEp](
                    const boost::system::error_code& ec,
                    const boost::container::flat_map<
                        std::string, dbus::utility::DbusVariantType>& props) {
                    auto objectPath = spdmEp->getObject();
                    const std::string descStr =
                        "Reading properties of " + objectPath + " object";
                    BMCWEB_LOG_DEBUG("{}", descStr);
                    if (ec)
                    {
                        BMCWEB_LOG_ERROR("{}: {}", descStr, ec.message());
                        errCallback(false, descStr, ec.message());
                        spdmEp->setError();
                        finalize();
                        return;
                    }
                    auto itSign = props.find("SignedMeasurements");
                    if (itSign == props.end())
                    {
                        errCallback(false, descStr,
                                    "cannot find SignedMeasurements property");
                        spdmEp->setError();
                        finalize();
                        return;
                    }
                    const auto* sign =
                        std::get_if<std::vector<uint8_t>>(&itSign->second);
                    if (sign == nullptr)
                    {
                        errCallback(
                            false, descStr,
                            "cannot decode SignedMeasurements property");
                        spdmEp->setError();
                        finalize();
                        return;
                    }
                    auto itCaps = props.find("Capabilities");
                    if (itCaps == props.end())
                    {
                        errCallback(false, descStr,
                                    "cannot find Capabilities property");
                        spdmEp->setError();
                        finalize();
                        return;
                    }
                    const auto* caps = std::get_if<uint32_t>(&itCaps->second);
                    if (caps == nullptr)
                    {
                        errCallback(false, descStr,
                                    "cannot decode Capabilities property");
                        spdmEp->setError();
                        finalize();
                        return;
                    }
                    std::string pem;
                    if ((*caps & spdmCertCapability) != 0U)
                    {
                        auto itCert = props.find("Certificate");
                        if (itCert == props.end())
                        {
                            errCallback(false, descStr,
                                        "cannot find Certificate property");
                            spdmEp->setError();
                            finalize();
                            return;
                        }
                        const auto* cert = std::get_if<
                            std::vector<std::tuple<uint8_t, std::string>>>(
                            &itCert->second);
                        if (cert == nullptr)
                        {
                            errCallback(false, descStr,
                                        "cannot decode Certificate property");
                            spdmEp->setError();
                            finalize();
                            return;
                        }
                        auto certSlot = std::find_if(
                            cert->begin(), cert->end(),
                            [](const auto& e) { return std::get<0>(e) == 0; });
                        if (certSlot == cert->end())
                        {
                            errCallback(false, descStr,
                                        "cannot find certificate for slot 0");
                            spdmEp->setError();
                            finalize();
                            return;
                        }
                        pem = std::get<1>(*certSlot);
                    }
                    std::vector<uint8_t> request;
                    request.reserve(sign->size() + pem.size());
                    request.insert(request.end(), sign->begin(), sign->end());
                    request.insert(request.end(), pem.begin(), pem.end());
                    spdmEp->setRequest(request);
                    finalize();
                    return;
                },
                spdmBusName, object, "org.freedesktop.DBus.Properties",
                "GetAll", spdmResponderIntf);
        }
        else if (startsWithPrefix(status, "Error_"))
        {
            errCallback(false, statusDescStr, status);
            ep->setError();
        }
        finalize();
    }

    void finalize()
    {
        const std::string desc = "Debug token request acquisition";
        BMCWEB_LOG_DEBUG("{}", desc);
        int completedRequestsCount = 0;
        if (nsmPending)
        {
            return;
        }
        for (const auto& ep : *endpoints)
        {
            auto state = ep->getState();
            if (isEndpointRequestPending(state))
            {
                return;
            }
            if (state == EndpointState::RequestAcquired)
            {
                ++completedRequestsCount;
            }
        }
        resetMatches();
        if (completedRequestsCount > 0)
        {
            resCallback(endpoints);
        }
        else
        {
            errCallback(true, desc, "No valid debug token request responses");
        }
    }
};

} // namespace debug_token
} // namespace redfish
