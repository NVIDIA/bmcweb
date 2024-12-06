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
#include "debug_token/request_utils.hpp"
#include "debug_token/status_utils.hpp"
#include "openbmc_dbus_rest.hpp"
#include "utils/dbus_utils.hpp"
#include "utils/mctp_utils.hpp"

#include <sys/stat.h>
#include <unistd.h>

#include <boost/asio.hpp>
#include <boost/process.hpp>
#include <boost/process/async.hpp>
#include <boost/process/child.hpp>

#include <functional>
#include <memory>
#include <sstream>
#include <vector>

namespace redfish::debug_token
{

// mctp-vdm-util's output size per endpoint
constexpr const size_t statusQueryOutputSize = 256;
constexpr const int statusQueryTimeoutSeconds = 60;

constexpr const std::string_view nsmDebugTokenSpecifier = "CRDT";

constexpr const auto nsmMatchRule =
    "type='signal',interface='org.freedesktop.DBus.Properties',"
    "member='PropertiesChanged',"
    "path_namespace='/xyz/openbmc_project/debug_token'";
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

    std::unique_ptr<sdbusplus::bus::match_t> nsmMatch;
    std::unique_ptr<sdbusplus::bus::match_t> spdmMatch;

    void createNsmMatch(const std::function<void(const std::string&,
                                                 const std::string&)>& callback)
    {
        nsmMatch = std::make_unique<sdbusplus::bus::match_t>(
            *crow::connections::systemBus, nsmMatchRule,
            [callback](sdbusplus::message_t& msg) {
            if (msg.is_method_error())
            {
                BMCWEB_LOG_ERROR("NSM match message error");
                return;
            }
            std::string object(msg.get_path());
            BMCWEB_LOG_DEBUG("NSM match handler: {}", object);
            std::string interface;
            std::map<std::string, dbus::utility::DbusVariantType> props;
            msg.read(interface, props);
            std::string* progressStatus = nullptr;
            if (interface == "xyz.openbmc_project.Common.Progress")
            {
                auto it = props.find("Status");
                if (it != props.end())
                {
                    progressStatus = std::get_if<std::string>(&(it->second));
                }
            }
            if (!progressStatus)
            {
                return;
            }
            if (*progressStatus ==
                "xyz.openbmc_project.Common.Progress.OperationStatus.InProgress")
            {
                return;
            }
            std::string status =
                progressStatus->substr(progressStatus->find_last_of('.') + 1);
            callback(object, status);
        });
    }

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
            if (!spdmStatus)
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
        nsmMatch.reset();
        spdmMatch.reset();
    }
};

class StatusQueryHandler : public OperationHandler
{
  public:
    StatusQueryHandler(ResultCallback&& resultCallback,
                       ErrorCallback&& errorCallback, bool useNsm = true)
    {
        BMCWEB_LOG_DEBUG("StatusQueryHandler constructor");
        resCallback = resultCallback;
        errCallback = errorCallback;
        mctp_utils::enumerateMctpEndpoints(
            [this](const std::shared_ptr<std::vector<mctp_utils::MctpEndpoint>>&
                       mctpEndpoints) {
            spdmEnumerationFinished = true;
            const std::string desc = "SPDM endpoint enumeration";
            BMCWEB_LOG_DEBUG("{}", desc);
            if (!mctpEndpoints || mctpEndpoints->size() == 0)
            {
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
            for (auto& ep : *mctpEndpoints)
            {
                if (!ep.isEnabled())
                {
                    continue;
                }
                const auto& msgTypes = ep.getMctpMessageTypes();
                if (std::find(msgTypes.begin(), msgTypes.end(),
                              mctp_utils::mctpMessageTypeVdm) != msgTypes.end())
                {
                    endpoints->emplace_back(
                        std::make_unique<DebugTokenSpdmEndpoint>(ep));
                }
            }
            endpoints->shrink_to_fit();
            getMctpVdmStatus();
            finalize();
        },
            [this, errorCallback](bool, const std::string& desc,
                                  const std::string& error) {
            spdmEnumerationFinished = true;
            errorCallback(false, desc, error);
            finalize();
        },
            "", static_cast<uint64_t>(statusQueryTimeoutSeconds) * 1000000u);

        if (!useNsm)
        {
            nsmEnumerationFinished = true;
            return;
        }
        constexpr std::array<std::string_view, 1> interfaces = {debugTokenIntf};
        dbus::utility::getSubTreePaths(
            "/xyz/openbmc_project/debug_token", 0, interfaces,
            [this](const boost::system::error_code& ec,
                   const dbus::utility::MapperGetSubTreePathsResponse& paths) {
            nsmEnumerationFinished = true;
            const std::string desc = "NSM endpoint enumeration";
            BMCWEB_LOG_DEBUG("{}", desc);
            if (ec)
            {
                BMCWEB_LOG_ERROR("{}: {}", desc, ec.message());
                finalize();
                return;
            }
            if (paths.size() == 0)
            {
                BMCWEB_LOG_ERROR("{}: {}", desc, "no endpoints found");
                finalize();
                return;
            }
            if (!endpoints)
            {
                endpoints = std::make_shared<
                    std::vector<std::unique_ptr<DebugTokenEndpoint>>>();
                endpoints->reserve(paths.size());
            }
            for (const auto& objectPath : paths)
            {
                endpoints->emplace_back(
                    std::make_unique<DebugTokenNsmEndpoint>(objectPath));
            }
            endpoints->shrink_to_fit();
            getNsmStatus();
            finalize();
        });
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
    std::unique_ptr<boost::process::child> subprocess;
    std::unique_ptr<boost::asio::steady_timer> subprocessTimer;
    std::vector<char> subprocessOutput;

    bool nsmEnumerationFinished{false};
    bool spdmEnumerationFinished{false};

    void getNsmStatus()
    {
        createNsmMatch(
            [this](const std::string& object, const std::string& status) {
            const std::string desc = "NSM token status acquisition for " +
                                     std::string(object);
            BMCWEB_LOG_DEBUG("{}", desc);
            auto endpoint = std::find_if(endpoints->begin(), endpoints->end(),
                                         [object](const auto& ep) {
                return ep->getType() == EndpointType::NSM &&
                       ep->getObject() == object;
            });
            if (endpoint == endpoints->end())
            {
                errCallback(false, desc, "unknown object");
                return;
            }
            auto& ep = *endpoint;
            auto state = ep->getState();
            if (state == EndpointState::Error ||
                state == EndpointState::StatusAcquired ||
                state == EndpointState::TokenInstalled)
            {
                errCallback(false, desc, "received unexpected update");
                return;
            }
            if (status == "Failed")
            {
                errCallback(false, desc, "operation rejected");
                ep->setError();
                finalize();
                return;
            }
            DebugTokenNsmEndpoint* nsmEp =
                static_cast<DebugTokenNsmEndpoint*>(ep.get());
            if (status == "Aborted")
            {
                sdbusplus::asio::getProperty<std::tuple<uint16_t, std::string>>(
                    *crow::connections::systemBus, "xyz.openbmc_project.NSM",
                    object, std::string(debugTokenIntf), "ErrorCode",
                    [this, desc, nsmEp, object](
                        const boost::system::error_code ec,
                        const std::tuple<uint16_t, std::string>& errorCode) {
                    const std::string desc =
                        object.substr(object.find_last_of('/') + 1);
                    BMCWEB_LOG_DEBUG("{}", desc);
                    if (ec)
                    {
                        BMCWEB_LOG_ERROR("{}: {}", desc, ec.message());
                        errCallback(false, desc, ec.message());
                        nsmEp->setError();
                        finalize();
                        return;
                    }
                    if (std::get<uint16_t>(errorCode) ==
                        debugTokenUnsupportedNsmErrorCode)
                    {
                        nsmEp->setStatus(EndpointState::DebugTokenUnsupported);
                        finalize();
                        return;
                    }
                    errCallback(false, desc, "operation failure");
                    nsmEp->setError();
                    finalize();
                    return;
                });
                return;
            }
            sdbusplus::asio::getProperty<NsmDbusTokenStatus>(
                *crow::connections::systemBus, "xyz.openbmc_project.NSM",
                object, std::string(debugTokenIntf), "TokenStatus",
                [this, desc, nsmEp,
                 object](const boost::system::error_code ec,
                         const NsmDbusTokenStatus& dbusStatus) {
                const std::string desc = "NSM get call for " +
                                         std::string(object);
                BMCWEB_LOG_DEBUG("{}", desc);
                if (ec)
                {
                    BMCWEB_LOG_ERROR("{}: {}", desc, ec.message());
                    errCallback(false, desc, ec.message());
                    nsmEp->setError();
                    finalize();
                    return;
                }
                try
                {
                    nsmEp->setStatus(
                        std::make_unique<NsmTokenStatus>(dbusStatus));
                }
                catch (std::exception&)
                {
                    nsmEp->setError();
                }
                finalize();
            });
        });

        bool getStatusIssued = false;
        for (auto& ep : *endpoints)
        {
            if (ep->getType() != EndpointType::NSM)
            {
                continue;
            }
            DebugTokenNsmEndpoint* nsmEp =
                static_cast<DebugTokenNsmEndpoint*>(ep.get());
            auto objectPath = ep->getObject();
            crow::connections::systemBus->async_method_call(
                [this, nsmEp, objectPath](const boost::system::error_code& ec) {
                const std::string desc = "NSM GetStatus call for " + objectPath;
                BMCWEB_LOG_DEBUG("{}", desc);
                if (ec)
                {
                    BMCWEB_LOG_ERROR("{}: {}", desc, ec.message());
                    errCallback(false, desc, ec.message());
                    nsmEp->setError();
                    finalize();
                }
            },
                "xyz.openbmc_project.NSM", objectPath,
                std::string(debugTokenIntf), "GetStatus",
                std::string(debugTokenTypesEnumPrefix) +
                    std::string(nsmDebugTokenSpecifier));
            getStatusIssued = true;
        }
        if (!getStatusIssued)
        {
            nsmMatch.reset();
        }
    }

    void subprocessExitCallback(int exitCode, const std::error_code& ec)
    {
        const std::string desc = "VDM token status query";
        BMCWEB_LOG_DEBUG("{}", desc);
        subprocessTimer.reset();
        if (ec)
        {
            BMCWEB_LOG_ERROR("{}: {}", desc, ec.message());
            errCallback(true, desc, ec.message());
            return;
        }
        if (exitCode != 0)
        {
            // If error is encountered mctp message will not have the proper
            // response for debug token, TX/RX parsing below will handle the
            // error messages
            BMCWEB_LOG_ERROR("{}: {}", desc, exitCode);
        }
        std::map<int, VdmTokenStatus> outputMap =
            parseVdmUtilWrapperOutput(subprocessOutput);
        for (auto& [eid, vdmStatus] : outputMap)
        {
            // report errors if found
            if (vdmStatus.responseStatus == VdmResponseStatus::INVALID_LENGTH ||
                vdmStatus.responseStatus == VdmResponseStatus::PROCESSING_ERROR)
            {
                errCallback(false, desc,
                            "Invalid status query data for EID " +
                                std::to_string(eid));
            }
            else if (vdmStatus.responseStatus == VdmResponseStatus::ERROR)
            {
                errCallback(false, desc,
                            "Error code received for EID " +
                                std::to_string(eid) + ": " +
                                std::to_string(*vdmStatus.errorCode));
            }
            else if (vdmStatus.tokenStatus ==
                     VdmTokenInstallationStatus::INVALID)
            {
                errCallback(false, desc,
                            "Invalid token status for EID " +
                                std::to_string(eid));
            }
            auto ep = std::find_if(endpoints->begin(), endpoints->end(),
                                   [eid](const auto& ep) {
                return ep->getType() == EndpointType::SPDM &&
                       ep->getMctpEid() == eid;
            });
            if (ep == endpoints->end())
            {
                continue;
            }
            DebugTokenSpdmEndpoint* spdmEp =
                static_cast<DebugTokenSpdmEndpoint*>(ep->get());
            spdmEp->setStatus(std::make_unique<VdmTokenStatus>(vdmStatus));
        }
        finalize();
    }

    void getMctpVdmStatus()
    {
        const std::string desc = "VDM token status query";
        BMCWEB_LOG_DEBUG("{}", desc);
        subprocessTimer = std::make_unique<boost::asio::steady_timer>(
            crow::connections::systemBus->get_io_context());
        subprocessTimer->expires_after(
            std::chrono::seconds(statusQueryTimeoutSeconds));
        subprocessTimer->async_wait(
            [this, desc](const boost::system::error_code ec) {
            if (ec && ec != boost::asio::error::operation_aborted)
            {
                if (subprocess)
                {
                    subprocess.reset();
                    errCallback(true, desc, "Timeout");
                }
            }
        });

        std::vector<std::string> args;
        for (const auto& ep : *endpoints)
        {
            if (ep->getType() != EndpointType::SPDM)
            {
                continue;
            }
            auto mctpEid = ep->getMctpEid();
            if (mctpEid != -1)
            {
                args.emplace_back(std::to_string(mctpEid));
            }
        }
        if (args.size() == 0)
        {
            errCallback(false, desc, "no valid endpoints");
            finalize();
            return;
        }
        try
        {
            subprocessOutput.resize(statusQueryOutputSize * args.size());
            auto callback = [this](int exitCode, const std::error_code& ec) {
                subprocessExitCallback(exitCode, ec);
            };
            subprocess = std::make_unique<boost::process::child>(
                "/usr/bin/mctp-vdm-util-token-status-query-wrapper.sh", args,
                boost::process::std_err > boost::process::null,
                boost::process::std_out > boost::asio::buffer(subprocessOutput),
                crow::connections::systemBus->get_io_context(),
                boost::process::on_exit = std::move(callback));
        }
        catch (const std::runtime_error& e)
        {
            errCallback(false, desc, e.what());
        }
    }

    void finalize()
    {
        const std::string desc = "Token status query processing";
        BMCWEB_LOG_DEBUG("{}", desc);
        if (!nsmEnumerationFinished || !spdmEnumerationFinished)
        {
            return;
        }
        if (!endpoints || endpoints->size() == 0)
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
                   ErrorCallback&& errorCallback, RequestType type) :
        type(type)
    {
        BMCWEB_LOG_DEBUG("RequestHandler constructor");
        resCallback = resultCallback;
        errCallback = errorCallback;
        statusHandler = std::make_unique<StatusQueryHandler>(
            [this, type](const std::shared_ptr<std::vector<
                             std::unique_ptr<DebugTokenEndpoint>>>& endpoints) {
            if (!endpoints || endpoints->size() == 0)
            {
                errCallback(true, "Debug token status check",
                            "No valid endpoints");
                return;
            }
            this->endpoints = endpoints;

            bool nsmRequestStarted = type == RequestType::DebugTokenRequest &&
                                     getNsmRequest();
            bool spdmRequestStarted = getSpdmRequest();
            if (!nsmRequestStarted && !spdmRequestStarted)
            {
                resCallback(endpoints);
            }
        },
            [errorCallback](bool critical, const std::string& desc,
                            const std::string& error) {
            errorCallback(critical, desc, error);
        },
            type == RequestType::DebugTokenRequest);
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
                requests.emplace_back(std::move(ep->getRequest()));
            }
        }
        auto file = generateTokenRequestFile(requests);
        result = std::string(file.begin(), file.end());
    }

  private:
    std::unique_ptr<StatusQueryHandler> statusHandler;

    RequestType type;

    uint8_t typeToMeasurementIndex(RequestType type)
    {
        static const std::map<RequestType, uint8_t> indexMap{
            {RequestType::DebugTokenRequest, 50},
            {RequestType::DOTCAKUnlockTokenRequest, 58},
            {RequestType::DOTEnableTokenRequest, 59},
            {RequestType::DOTSignTestToken, 60},
            {RequestType::DOTOverrideTokenRequest, 61}};

        return indexMap.at(type);
    }

    bool getNsmRequest()
    {
        createNsmMatch(
            [this](const std::string& object, const std::string& status) {
            nsmUpdate(object, status);
        });

        bool getRequestIssued = false;
        for (auto& ep : *endpoints)
        {
            auto type = ep->getType();
            auto state = ep->getState();
            if (type != EndpointType::NSM ||
                state != EndpointState::StatusAcquired)
            {
                continue;
            }
            DebugTokenNsmEndpoint* nsmEp =
                static_cast<DebugTokenNsmEndpoint*>(ep.get());
            auto objectPath = ep->getObject();
            crow::connections::systemBus->async_method_call(
                [this, nsmEp, objectPath](const boost::system::error_code& ec) {
                const std::string desc = "NSM GetStatus call for " + objectPath;
                BMCWEB_LOG_DEBUG("{}", desc);
                if (ec)
                {
                    BMCWEB_LOG_ERROR("{}: {}", desc, ec.message());
                    errCallback(false, desc, ec.message());
                    nsmEp->setError();
                    finalize();
                }
            },
                "xyz.openbmc_project.NSM", objectPath,
                std::string(debugTokenIntf), "GetRequest",
                std::string(debugTokenOpcodesEnumPrefix) +
                    std::string(nsmDebugTokenSpecifier));
            getRequestIssued = true;
        }
        if (!getRequestIssued)
        {
            nsmMatch.reset();
        }

        return getRequestIssued;
    }

    bool getSpdmRequest()
    {
        createSpdmMatch(
            [this](const std::string& object, const std::string& status) {
            spdmUpdate(object, status);
        });
        std::vector<uint8_t> indices{typeToMeasurementIndex(type)};
        bool refreshIssued = false;
        for (auto& ep : *endpoints)
        {
            auto type = ep->getType();
            auto state = ep->getState();
            if (type != EndpointType::SPDM ||
                state != EndpointState::StatusAcquired)
            {
                continue;
            }
            DebugTokenSpdmEndpoint* spdmEp =
                static_cast<DebugTokenSpdmEndpoint*>(ep.get());
            auto objectPath = ep->getObject();
            const std::string desc = "SPDM refresh call for " + objectPath;
            BMCWEB_LOG_DEBUG("{}", desc);
            crow::connections::systemBus->async_method_call(
                [this, desc, spdmEp](const boost::system::error_code ec) {
                if (ec)
                {
                    BMCWEB_LOG_ERROR("{}: {}", desc, ec.message());
                    errCallback(false, desc, ec.message());
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
        }

        return refreshIssued;
    }

    void nsmUpdate(const std::string& object, const std::string& status)
    {
        const std::string desc = "Token request acquisition for " +
                                 std::string(object);
        BMCWEB_LOG_DEBUG("{}", desc);
        auto endpoint = std::find_if(endpoints->begin(), endpoints->end(),
                                     [object](const auto& ep) {
            return ep->getType() == EndpointType::NSM &&
                   ep->getObject() == object;
        });
        if (endpoint == endpoints->end())
        {
            errCallback(false, desc, "unknown object");
            return;
        }
        auto& ep = *endpoint;
        auto state = ep->getState();
        if (state == EndpointState::Error ||
            state == EndpointState::TokenInstalled ||
            state == EndpointState::RequestAcquired)
        {
            errCallback(false, desc, "received unexpected update");
            return;
        }
        if (status == "Failed")
        {
            errCallback(false, desc, "operation rejected");
            ep->setError();
            finalize();
            return;
        }
        DebugTokenNsmEndpoint* nsmEp =
            static_cast<DebugTokenNsmEndpoint*>(ep.get());
        if (status == "Aborted")
        {
            sdbusplus::asio::getProperty<std::tuple<uint16_t, std::string>>(
                *crow::connections::systemBus, "xyz.openbmc_project.NSM",
                object, std::string(debugTokenIntf), "ErrorCode",
                [this, desc, object,
                 nsmEp](const boost::system::error_code ec,
                        const std::tuple<uint16_t, std::string>& errorCode) {
                const std::string desc =
                    object.substr(object.find_last_of('/') + 1);
                BMCWEB_LOG_DEBUG("{}", desc);
                if (ec)
                {
                    BMCWEB_LOG_ERROR("{}: {}", desc, ec.message());
                    errCallback(false, desc, ec.message());
                    nsmEp->setError();
                    finalize();
                    return;
                }
                static const uint16_t unsupportedErrorCode = 5;
                if (std::get<uint16_t>(errorCode) == unsupportedErrorCode)
                {
                    nsmEp->setStatus(EndpointState::DebugTokenUnsupported);
                    finalize();
                    return;
                }
                errCallback(false, desc, "operation failure");
                nsmEp->setError();
                finalize();
                return;
            });
            return;
        }
        sdbusplus::asio::getProperty<sdbusplus::message::unix_fd>(
            *crow::connections::systemBus, "xyz.openbmc_project.NSM", object,
            std::string(debugTokenIntf), "RequestFd",
            [this, object, nsmEp](const boost::system::error_code ec,
                                  const sdbusplus::message::unix_fd& unixfd) {
            const std::string desc = "NSM get call for " + std::string(object);
            BMCWEB_LOG_DEBUG("{}", desc);
            if (ec)
            {
                BMCWEB_LOG_ERROR("{}: {}", desc, ec.message());
                errCallback(false, desc, ec.message());
                nsmEp->setError();
                finalize();
                return;
            }
            BMCWEB_LOG_DEBUG("Received fd: {}", unixfd.fd);
            std::vector<uint8_t> request;
            if (readNsmTokenRequestFd(unixfd.fd, request))
            {
                nsmEp->setRequest(request);
            }
            else
            {
                errCallback(false, desc, "request file operation failure");
                nsmEp->setError();
            }
            finalize();
        });
    }

    void spdmUpdate(const std::string& object, const std::string& status)
    {
        const std::string desc = "Update of " + object +
                                 " object with status " + status;
        BMCWEB_LOG_DEBUG("{}", desc);
        auto endpoint = std::find_if(endpoints->begin(), endpoints->end(),
                                     [object](const auto& ep) {
            return ep->getType() == EndpointType::SPDM &&
                   ep->getObject() == object;
        });
        if (endpoint == endpoints->end())
        {
            errCallback(false, desc, "unknown object");
            return;
        }
        auto& ep = *endpoint;
        auto state = ep->getState();
        if (state == EndpointState::Error ||
            state == EndpointState::TokenInstalled ||
            state == EndpointState::RequestAcquired)
        {
            errCallback(false, desc, "received unexpected update");
        }
        else if (status == "Success")
        {
            DebugTokenSpdmEndpoint* spdmEp =
                static_cast<DebugTokenSpdmEndpoint*>(ep.get());
            crow::connections::systemBus->async_method_call(
                [this, spdmEp](
                    const boost::system::error_code ec,
                    const boost::container::flat_map<
                        std::string, dbus::utility::DbusVariantType>& props) {
                auto objectPath = spdmEp->getObject();
                const std::string desc = "Reading properties of " + objectPath +
                                         " object";
                BMCWEB_LOG_DEBUG("{}", desc);
                if (ec)
                {
                    BMCWEB_LOG_ERROR("{}: {}", desc, ec.message());
                    errCallback(false, desc, ec.message());
                    spdmEp->setError();
                    finalize();
                    return;
                }
                auto itSign = props.find("SignedMeasurements");
                if (itSign == props.end())
                {
                    errCallback(false, desc,
                                "cannot find SignedMeasurements property");
                    spdmEp->setError();
                    finalize();
                    return;
                }
                auto sign = std::get_if<std::vector<uint8_t>>(&itSign->second);
                if (!sign)
                {
                    errCallback(false, desc,
                                "cannot decode SignedMeasurements property");
                    spdmEp->setError();
                    finalize();
                    return;
                }
                auto itCaps = props.find("Capabilities");
                if (itCaps == props.end())
                {
                    errCallback(false, desc,
                                "cannot find Capabilities property");
                    spdmEp->setError();
                    finalize();
                    return;
                }
                auto caps = std::get_if<uint32_t>(&itCaps->second);
                if (!caps)
                {
                    errCallback(false, desc,
                                "cannot decode Capabilities property");
                    spdmEp->setError();
                    finalize();
                    return;
                }
                std::string pem;
                if (*caps & spdmCertCapability)
                {
                    auto itCert = props.find("Certificate");
                    if (itCert == props.end())
                    {
                        errCallback(false, desc,
                                    "cannot find Certificate property");
                        spdmEp->setError();
                        finalize();
                        return;
                    }
                    auto cert = std::get_if<
                        std::vector<std::tuple<uint8_t, std::string>>>(
                        &itCert->second);
                    if (!cert)
                    {
                        errCallback(false, desc,
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
                        errCallback(false, desc,
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
            errCallback(false, desc, status);
            ep->setError();
        }
        finalize();
    }

    void finalize()
    {
        const std::string desc = "Debug token request acquisition";
        BMCWEB_LOG_DEBUG("{}", desc);
        int completedRequestsCount = 0;
        for (const auto& ep : *endpoints)
        {
            auto state = ep->getState();
            if (state == EndpointState::StatusAcquired)
            {
                return;
            }
            if (state == EndpointState::RequestAcquired ||
                state == EndpointState::TokenInstalled)
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

} // namespace redfish::debug_token
