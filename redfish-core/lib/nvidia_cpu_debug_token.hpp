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
#include "debug_token/install_utils.hpp"
#include "debug_token/request_utils.hpp"
#include "debug_token/status_utils.hpp"
#include "mctp_vdm_util_wrapper.hpp"
#include "nvidia_messages.hpp"
#include "query.hpp"
#include "registries/privilege_registry.hpp"
#include "utils/mctp_utils.hpp"

#include <map>
#include <memory>

namespace redfish
{

namespace debug_token
{

constexpr const int cpuTokenGenerationTimeoutSeconds = 30;
constexpr const uint8_t cpuTokenGenerationSlotId = 0;
constexpr const uint8_t cpuTokenGenerationMeasIndex = 50;

inline void getCpuObjectPath(
    std::function<void(const boost::system::error_code&, const std::string)>&&
        callback)
{
    sdbusplus::message::object_path path(rootSPDMDbusPath);
    dbus::utility::getManagedObjects(
        spdmBusName, path,
        [callback{std::move(callback)}](
            const boost::system::error_code& ec,
            const dbus::utility::ManagedObjectType& objects) {
            if (ec)
            {
                callback(ec, std::string());
                return;
            }
            for (const auto& [objectPath, interfaces] : objects)
            {
                const bool* enabled = nullptr;
                const uint32_t* capabilities = nullptr;
                for (const auto& [interface, properties] : interfaces)
                {
                    if (interface != "xyz.openbmc_project.Object.Enable" &&
                        interface != "xyz.openbmc_project.SPDM.Responder")
                    {
                        continue;
                    }
                    for (const auto& [name, value] : properties)
                    {
                        if (name == "Enabled")
                        {
                            enabled = std::get_if<bool>(&value);
                            if (enabled == nullptr)
                            {
                                BMCWEB_LOG_ERROR(
                                    "Enabled property value is null");
                            }
                        }
                        if (name == "Capabilities")
                        {
                            capabilities = std::get_if<uint32_t>(&value);
                            if (capabilities == nullptr)
                            {
                                BMCWEB_LOG_ERROR(
                                    "Capabilities property value is null");
                            }
                        }
                    }
                }
                if (enabled != nullptr && capabilities != nullptr &&
                    *enabled == false &&
                    (*capabilities & spdmCertCapability) == 0)
                {
                    callback(ec, objectPath.str);
                    return;
                }
            }
            callback(ec, std::string());
        });
}

inline void getCpuEid(const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                      std::function<void(uint32_t)>&& callback)
{
    getCpuObjectPath([asyncResp, callback](const boost::system::error_code& ec,
                                           const std::string path) {
        if (ec || path.empty())
        {
            BMCWEB_LOG_ERROR("Failed to find CPU object path: {}",
                             ec.message());
            messages::internalError(asyncResp->res);
            return;
        }
        auto objectName = sdbusplus::message::object_path(path).filename();
        mctp_utils::enumerateMctpEndpoints(
            [asyncResp, callback](
                const std::shared_ptr<std::vector<mctp_utils::MctpEndpoint>>&
                    endpoints) {
                if (!endpoints || endpoints->size() == 0)
                {
                    BMCWEB_LOG_ERROR("Failed to find CPU MCTP EID");
                    messages::internalError(asyncResp->res);
                    return;
                }
                uint32_t eid =
                    static_cast<uint32_t>(endpoints->begin()->getMctpEid());
                callback(eid);
            },
            [asyncResp](bool critical, const std::string& desc,
                        const std::string& msg) {
                if (critical)
                {
                    BMCWEB_LOG_ERROR("{}: {}", desc, msg);
                    messages::internalError(asyncResp->res);
                }
            },
            objectName);
    });
}

inline void
    getSystemsCpuDebugToken(const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                            const std::string& systemName)
{
    if (systemName != BMCWEB_REDFISH_SYSTEM_URI_NAME)
    {
        return;
    }
    getCpuObjectPath([asyncResp,
                      systemName](const boost::system::error_code& ec,
                                  const std::string path) {
        if (ec)
        {
            BMCWEB_LOG_ERROR("Failed to find CPU object path: {}",
                             ec.message());
            return;
        }
        if (!path.empty())
        {
            auto& oemNvidia = asyncResp->res.jsonValue["Oem"]["Nvidia"];
            oemNvidia["@odata.type"] =
                "#NvidiaComputerSystem.v1_2_0.NvidiaComputerSystem";
            oemNvidia["CPUDebugToken"]["@odata.id"] = boost::urls::format(
                "/redfish/v1/Systems/{}/Oem/Nvidia/CPUDebugToken", systemName);
        }
    });
}

inline void handleCpuDebugTokenResourceInfo(
    App& app, const crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& systemName)
{
    using namespace std::string_literals;
    if (!redfish::setUpRedfishRoute(app, req, asyncResp))
    {
        return;
    }
    if (systemName != BMCWEB_REDFISH_SYSTEM_URI_NAME)
    {
        messages::resourceNotFound(asyncResp->res, "ComputerSystem",
                                   systemName);
        return;
    }
    getCpuEid(asyncResp, [req, asyncResp, systemName](uint32_t eid) {
        MctpVdmUtil mctpVdmUtilWrapper(eid);
        mctpVdmUtilWrapper.run(
            MctpVdmUtilCommand::DEBUG_TOKEN_QUERY, std::monostate(), req,
            asyncResp,
            [systemName](const crow::Request& req,
                         const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                         uint32_t, const std::string& stdOut,
                         const std::string&,
                         const boost::system::error_code& ec, int errorCode) {
                if (ec || errorCode)
                {
                    BMCWEB_LOG_ERROR("mctp-vdm-util error: {} {}", ec.message(),
                                     errorCode);
                    messages::internalError(asyncResp->res);
                    return;
                }
                std::size_t rxPos = stdOut.find("RX: ");
                if (rxPos == std::string::npos)
                {
                    BMCWEB_LOG_ERROR("Invalid VDM command response: {}",
                                     stdOut);
                    messages::internalError(asyncResp->res);
                    return;
                }
                std::string rxData = stdOut.substr(rxPos + 4);
                VdmTokenStatus status(rxData, 1);
                if (status.responseStatus ==
                        VdmResponseStatus::INVALID_LENGTH ||
                    status.responseStatus ==
                        VdmResponseStatus::PROCESSING_ERROR ||
                    status.tokenStatus == VdmTokenInstallationStatus::INVALID)
                {
                    BMCWEB_LOG_ERROR("Invalid VDM command response: {}",
                                     stdOut);
                    messages::internalError(asyncResp->res);
                    return;
                }
                if (status.responseStatus == VdmResponseStatus::ERROR)
                {
                    BMCWEB_LOG_ERROR("VDM error code: {}", *status.errorCode);
                    messages::internalError(asyncResp->res);
                    return;
                }
                if (status.responseStatus == VdmResponseStatus::NOT_SUPPORTED)
                {
                    messages::debugTokenUnsupported(asyncResp->res, systemName);
                    return;
                }
                auto& resJson = asyncResp->res.jsonValue;
                if (status.tokenStatus == VdmTokenInstallationStatus::INSTALLED)
                {
                    resJson["Status"] = "DebugSessionActive";
                }
                else
                {
                    resJson["Status"] = "NoTokenApplied";
                }
                resJson["TokenType"] = "CRCS";
                std::string resUri{req.url().buffer()};
                resJson["@odata.type"] =
                    "#NvidiaDebugToken.v1_0_0.NvidiaDebugToken";
                resJson["@odata.id"] = resUri;
                resJson["Id"] = "CPUDebugToken";
                resJson["Name"] = systemName + " Debug Token Resource"s;

                auto& actions = resJson["Actions"];
                auto& generateAction =
                    actions["#NvidiaDebugToken.GenerateToken"];
                generateAction["target"] =
                    resUri + "/Actions/NvidiaDebugToken.GenerateToken"s;
                generateAction["@Redfish.ActionInfo"] =
                    resUri + "/GenerateTokenActionInfo"s;
                auto& installAction = actions["#NvidiaDebugToken.InstallToken"];
                installAction["target"] =
                    resUri + "/Actions/NvidiaDebugToken.InstallToken"s;
                installAction["@Redfish.ActionInfo"] =
                    resUri + "/InstallTokenActionInfo"s;
                auto& disableAction = actions["#NvidiaDebugToken.DisableToken"];
                disableAction["target"] =
                    resUri + "/Actions/NvidiaDebugToken.DisableToken"s;
            });
    });
}

inline void handleCpuGenerateTokenActionInfo(
    App& app, const crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& systemName)
{
    if (!redfish::setUpRedfishRoute(app, req, asyncResp))
    {
        return;
    }
    if (systemName != BMCWEB_REDFISH_SYSTEM_URI_NAME)
    {
        messages::resourceNotFound(asyncResp->res, "ComputerSystem",
                                   systemName);
        return;
    }
    asyncResp->res.jsonValue["@odata.type"] = "#ActionInfo.v1_2_0.ActionInfo";
    asyncResp->res.jsonValue["@odata.id"] = req.url();
    asyncResp->res.jsonValue["Id"] = "GenerateTokenActionInfo";
    asyncResp->res.jsonValue["Name"] = "GenerateToken Action Info";

    nlohmann::json::array_t parameters;
    nlohmann::json::object_t parameter;
    parameter["Name"] = "TokenType";
    parameter["Required"] = true;
    parameter["DataType"] = "String";
    nlohmann::json::array_t allowed;
    allowed.emplace_back("CRCS");
    parameter["AllowableValues"] = std::move(allowed);
    parameters.emplace_back(std::move(parameter));

    asyncResp->res.jsonValue["Parameters"] = std::move(parameters);
}

inline void handleCpuInstallTokenActionInfo(
    App& app, const crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& systemName)
{
    if (!redfish::setUpRedfishRoute(app, req, asyncResp))
    {
        return;
    }
    if (systemName != BMCWEB_REDFISH_SYSTEM_URI_NAME)
    {
        messages::resourceNotFound(asyncResp->res, "ComputerSystem",
                                   systemName);
        return;
    }
    asyncResp->res.jsonValue["@odata.type"] = "#ActionInfo.v1_2_0.ActionInfo";
    asyncResp->res.jsonValue["@odata.id"] = req.url();
    asyncResp->res.jsonValue["Id"] = "InstallTokenActionInfo";
    asyncResp->res.jsonValue["Name"] = "InstallToken Action Info";

    nlohmann::json::array_t parameters;
    nlohmann::json::object_t parameter;
    parameter["Name"] = "TokenData";
    parameter["Required"] = true;
    parameter["DataType"] = "String";
    parameters.emplace_back(std::move(parameter));

    asyncResp->res.jsonValue["Parameters"] = std::move(parameters);
}

inline void
    handleCpuDisableToken(App& app, const crow::Request& req,
                          const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                          const std::string& systemName)
{
    if (!redfish::setUpRedfishRoute(app, req, asyncResp))
    {
        return;
    }
    if (systemName != BMCWEB_REDFISH_SYSTEM_URI_NAME)
    {
        messages::resourceNotFound(asyncResp->res, "ComputerSystem",
                                   systemName);
        return;
    }
    getCpuEid(asyncResp, [req, asyncResp, systemName](uint32_t eid) {
        MctpVdmUtil mctpVdmUtilWrapper(eid);
        mctpVdmUtilWrapper.run(
            MctpVdmUtilCommand::DEBUG_TOKEN_ERASE, std::monostate(), req,
            asyncResp,
            [](const crow::Request& req,
               const std::shared_ptr<bmcweb::AsyncResp>& asyncResp, uint32_t,
               const std::string& stdOut, const std::string&,
               const boost::system::error_code& ec, int errorCode) {
                if (ec || errorCode)
                {
                    BMCWEB_LOG_ERROR("mctp-vdm-util error: {} {}", ec.message(),
                                     errorCode);
                    messages::internalError(asyncResp->res);
                    return;
                }
                std::size_t rxPos = stdOut.find("RX: ");
                if (rxPos == std::string::npos)
                {
                    BMCWEB_LOG_ERROR("Invalid VDM command response: {}",
                                     stdOut);
                    messages::internalError(asyncResp->res);
                    return;
                }
                std::string rxData = stdOut.substr(rxPos + 4);
                std::istringstream iss{rxData};
                std::vector<std::string> bytes{
                    std::istream_iterator<std::string>{iss},
                    std::istream_iterator<std::string>{}};
                if (bytes.size() != MctpVdmUtilErrorCodeOffset + 1)
                {
                    BMCWEB_LOG_ERROR("Invalid VDM command response: {}",
                                     rxData);
                    messages::internalError(asyncResp->res);
                    return;
                }
                try
                {
                    int vdmCode = std::stoi(bytes[MctpVdmUtilErrorCodeOffset]);
                    if (vdmCode == 0)
                    {
                        messages::success(asyncResp->res);
                        return;
                    }
                    messages::resourceErrorsDetectedFormatError(
                        asyncResp->res, req.url().buffer(),
                        "VDM command error");
                }
                catch (std::exception&)
                {
                    BMCWEB_LOG_ERROR("Invalid VDM command response: {}",
                                     rxData);
                    messages::internalError(asyncResp->res);
                }
            });
    });
}

static std::unique_ptr<sdbusplus::bus::match_t> match;
static std::unique_ptr<boost::asio::steady_timer> timer;
inline void
    handleCpuGenerateToken(App& app, const crow::Request& req,
                           const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                           const std::string& systemName)
{
    if (!redfish::setUpRedfishRoute(app, req, asyncResp))
    {
        return;
    }
    if (systemName != BMCWEB_REDFISH_SYSTEM_URI_NAME)
    {
        messages::resourceNotFound(asyncResp->res, "ComputerSystem",
                                   systemName);
        return;
    }
    std::string tokenType;
    if (!redfish::json_util::readJsonAction(req, asyncResp->res, "TokenType",
                                            tokenType))
    {
        return;
    }
    if (tokenType != "CRCS")
    {
        messages::actionParameterValueNotInList(asyncResp->res, tokenType,
                                                "TokenType", "GenerateToken");
        return;
    }
    getCpuObjectPath([asyncResp](const boost::system::error_code& ec,
                                 const std::string path) {
        if (ec || path.empty())
        {
            BMCWEB_LOG_ERROR("Failed to find CPU object path: {}",
                             ec.message());
            messages::internalError(asyncResp->res);
            return;
        }
        timer = std::make_unique<boost::asio::steady_timer>(
            crow::connections::systemBus->get_io_context());
        timer->expires_after(
            std::chrono::seconds(cpuTokenGenerationTimeoutSeconds));
        timer->async_wait([asyncResp](const boost::system::error_code& ec) {
            match.reset(nullptr);
            boost::asio::post(crow::connections::systemBus->get_io_context(),
                              [] { timer.reset(nullptr); });
            if (!ec)
            {
                BMCWEB_LOG_ERROR("CPU debug token generation timeout");
                messages::internalError(asyncResp->res);
                return;
            }
            if (ec != boost::asio::error::operation_aborted)
            {
                BMCWEB_LOG_ERROR("async_wait error {}", ec);
                messages::internalError(asyncResp->res);
            }
        });
        std::string matchRule(
            "type='signal',"
            "interface='org.freedesktop.DBus.Properties',"
            "path='" +
            path +
            "',"
            "member='PropertiesChanged',"
            "arg0=" +
            spdmResponderIntf);
        match = std::make_unique<sdbusplus::bus::match_t>(
            *crow::connections::systemBus, matchRule,
            [asyncResp, path](sdbusplus::message_t& msg) {
                std::string interface;
                std::map<std::string, dbus::utility::DbusVariantType> props;
                msg.read(interface, props);
                std::string opStatus;
                if (interface == spdmResponderIntf)
                {
                    auto it = props.find("Status");
                    if (it != props.end())
                    {
                        auto status = std::get_if<std::string>(&(it->second));
                        if (status)
                        {
                            opStatus =
                                status->substr(status->find_last_of('.') + 1);
                        }
                    }
                }
                if (opStatus.empty())
                {
                    return;
                }
                if (opStatus.rfind("Error_", 0) == 0)
                {
                    timer.reset(nullptr);
                    boost::asio::post(
                        crow::connections::systemBus->get_io_context(),
                        [] { match.reset(nullptr); });
                    BMCWEB_LOG_ERROR(
                        "Token generation for CPU failed, end status: {}",
                        opStatus);
                    messages::internalError(asyncResp->res);
                    return;
                }
                if (opStatus == "Success")
                {
                    timer.reset(nullptr);
                    boost::asio::post(
                        crow::connections::systemBus->get_io_context(),
                        [] { match.reset(nullptr); });
                    sdbusplus::asio::getProperty<std::vector<uint8_t>>(
                        *crow::connections::systemBus, spdmBusName, path,
                        spdmResponderIntf, "SignedMeasurements",
                        [asyncResp](const boost::system::error_code ec,
                                    const std::vector<uint8_t>& meas) {
                            if (ec)
                            {
                                BMCWEB_LOG_ERROR(
                                    "Failed to get SignedMeasurements for CPU: {}",
                                    ec.message());
                                messages::internalError(asyncResp->res);
                                return;
                            }
                            std::vector<std::vector<uint8_t>> requestVec{
                                addTokenRequestHeader(meas)};
                            auto file = generateTokenRequestFile(requestVec);
                            std::string_view binaryData(
                                reinterpret_cast<const char*>(file.data()),
                                file.size());
                            asyncResp->res.jsonValue["Token"] =
                                crow::utility::base64encode(binaryData);
                        });
                }
            });
        std::vector<uint8_t> indices{cpuTokenGenerationMeasIndex};
        crow::connections::systemBus->async_method_call(
            [asyncResp](const boost::system::error_code ec) {
                if (ec)
                {
                    BMCWEB_LOG_ERROR("Failed to issue Refresh for CPU: {}",
                                     ec.message());
                    match.reset(nullptr);
                    timer.reset(nullptr);
                    messages::internalError(asyncResp->res);
                    return;
                }
            },
            spdmBusName, path, spdmResponderIntf, "Refresh",
            cpuTokenGenerationSlotId, std::vector<uint8_t>(), indices,
            static_cast<uint32_t>(0));
    });
}

inline void
    handleCpuInstallToken(App& app, const crow::Request& req,
                          const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                          const std::string& systemName)
{
    if (!redfish::setUpRedfishRoute(app, req, asyncResp))
    {
        return;
    }
    if (systemName != BMCWEB_REDFISH_SYSTEM_URI_NAME)
    {
        messages::resourceNotFound(asyncResp->res, "ComputerSystem",
                                   systemName);
        return;
    }
    std::string tokenData;
    if (!redfish::json_util::readJsonAction(req, asyncResp->res, "TokenData",
                                            tokenData))
    {
        return;
    }
    std::string binaryData;
    if (!crow::utility::base64Decode(tokenData, binaryData))
    {
        messages::actionParameterValueFormatError(asyncResp->res, tokenData,
                                                  "TokenData", "InstallToken");
        return;
    }
    auto dataVector =
        std::vector<uint8_t>(binaryData.begin(), binaryData.end());
    getCpuEid(asyncResp, [req, asyncResp, systemName,
                          dataVector](uint32_t eid) {
        MctpVdmUtil mctpVdmUtilWrapper(eid);
        mctpVdmUtilWrapper.run(
            MctpVdmUtilCommand::DEBUG_TOKEN_INSTALL, dataVector, req, asyncResp,
            [](const crow::Request& req,
               const std::shared_ptr<bmcweb::AsyncResp>& asyncResp, uint32_t,
               const std::string& stdOut, const std::string&,
               const boost::system::error_code& ec, int errorCode) {
                if (ec || errorCode)
                {
                    BMCWEB_LOG_ERROR("mctp-vdm-util error: {} {}", ec.message(),
                                     errorCode);
                    messages::internalError(asyncResp->res);
                    return;
                }
                std::size_t rxPos = stdOut.find("RX: ");
                if (rxPos == std::string::npos)
                {
                    BMCWEB_LOG_ERROR("Invalid VDM command response: {}",
                                     stdOut);
                    messages::internalError(asyncResp->res);
                    return;
                }
                std::string rxData = stdOut.substr(rxPos + 4);
                std::istringstream iss{rxData};
                std::vector<std::string> bytes{
                    std::istream_iterator<std::string>{iss},
                    std::istream_iterator<std::string>{}};
                if (bytes.size() != MctpVdmUtilErrorCodeOffset + 1)
                {
                    BMCWEB_LOG_ERROR("Invalid VDM command response: {}",
                                     rxData);
                    messages::internalError(asyncResp->res);
                    return;
                }
                try
                {
                    int vdmCode = std::stoi(bytes[MctpVdmUtilErrorCodeOffset]);
                    if (vdmCode == 0)
                    {
                        messages::success(asyncResp->res);
                        return;
                    }
                    messages::resourceErrorsDetectedFormatError(
                        asyncResp->res, req.url().buffer(),
                        getVdmDebugTokenInstallErrorDescription(vdmCode));
                }
                catch (std::exception&)
                {
                    BMCWEB_LOG_ERROR("Invalid VDM command response: {}",
                                     rxData);
                    messages::internalError(asyncResp->res);
                }
            });
    });
}

} // namespace debug_token

inline void requestRoutesCpuDebugToken(App& app)
{
    using namespace debug_token;
    BMCWEB_ROUTE(app, "/redfish/v1/Systems/<str>/Oem/Nvidia/CPUDebugToken/")
        .privileges(redfish::privileges::getComputerSystem)
        .methods(boost::beast::http::verb::get)(
            std::bind_front(handleCpuDebugTokenResourceInfo, std::ref(app)));
    BMCWEB_ROUTE(app, "/redfish/v1/Systems/<str>/Oem/Nvidia/CPUDebugToken"
                      "/GenerateTokenActionInfo/")
        .privileges(redfish::privileges::getComputerSystem)
        .methods(boost::beast::http::verb::get)(
            std::bind_front(handleCpuGenerateTokenActionInfo, std::ref(app)));
    BMCWEB_ROUTE(app, "/redfish/v1/Systems/<str>/Oem/Nvidia/CPUDebugToken"
                      "/InstallTokenActionInfo/")
        .privileges(redfish::privileges::getComputerSystem)
        .methods(boost::beast::http::verb::get)(
            std::bind_front(handleCpuInstallTokenActionInfo, std::ref(app)));
    BMCWEB_ROUTE(app, "/redfish/v1/Systems/<str>/Oem/Nvidia/CPUDebugToken"
                      "/Actions/NvidiaDebugToken.DisableToken")
        .privileges(redfish::privileges::postComputerSystem)
        .methods(boost::beast::http::verb::post)(
            std::bind_front(handleCpuDisableToken, std::ref(app)));
    BMCWEB_ROUTE(app, "/redfish/v1/Systems/<str>/Oem/Nvidia/CPUDebugToken"
                      "/Actions/NvidiaDebugToken.GenerateToken")
        .privileges(redfish::privileges::postComputerSystem)
        .methods(boost::beast::http::verb::post)(
            std::bind_front(handleCpuGenerateToken, std::ref(app)));
    BMCWEB_ROUTE(app, "/redfish/v1/Systems/<str>/Oem/Nvidia/CPUDebugToken"
                      "/Actions/NvidiaDebugToken.InstallToken")
        .privileges(redfish::privileges::postComputerSystem)
        .methods(boost::beast::http::verb::post)(
            std::bind_front(handleCpuInstallToken, std::ref(app)));
}

} // namespace redfish
