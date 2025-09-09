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
#include "debug_token/unified/action.hpp"
#include "debug_token/unified/request.hpp"
#include "debug_token/unified/utils.hpp"
#include "multipart_parser.hpp"
#include "nvidia_messages.hpp"
#include "nvidia_trusted_component_oem.hpp"
#include "query.hpp"
#include "registries/privilege_registry.hpp"
#include "update_service.hpp"
#include "utils/chassis_utils.hpp"
#include "utils/json_utils.hpp"
#include "utils/memfd_utils.hpp"

#include <cstdint>
#include <format>
#include <functional>
#include <memory>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <variant>

namespace redfish
{

namespace debug_token
{

// Cache for storing generated token data
// Key: "chassisId:componentId", Value: binary token data
inline std::unordered_map<std::string, std::vector<uint8_t>>&
    getTokenDataCache()
{
    static std::unordered_map<std::string, std::vector<uint8_t>> tokenDataCache;
    return tokenDataCache;
}

static void eraseInstallCallback(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& chassisId, const std::string& componentId,
    const unified::action::Result& result)
{
    if (std::holds_alternative<NsmResult>(result))
    {
        const auto& [code, message] = std::get<NsmResult>(result);
        if (code == debugTokenUnsupportedNsmErrorCode)
        {
            messages::debugTokenUnsupported(
                asyncResp->res,
                std::format("{} TrustedComponents {}", chassisId, componentId));
        }
        else if (code == debugTokenSuccessNsmErrorCode)
        {
            messages::success(asyncResp->res);
        }
        else
        {
            messages::resourceErrorsDetectedFormatError(
                asyncResp->res,
                std::format("{} TrustedComponents {}", chassisId, componentId),
                message);
        }
    }
    else
    {
        messages::internalError(asyncResp->res);
    }
}

static void generateTokenRequestCallback(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& chassisId, const std::string& componentId,
    const unified::spdm_request::Result& result)
{
    if (std::holds_alternative<std::vector<uint8_t>>(result))
    {
        // TLV structure already contains its own header, no need for additional
        // header
        auto tokenFile =
            generateTokenRequestFile({std::get<std::vector<uint8_t>>(result)});
        std::string cacheKey = std::format("{}:{}", chassisId, componentId);
        getTokenDataCache()[cacheKey] = std::move(tokenFile);
        asyncResp->res.jsonValue["BinaryTokenURI"] =
            std::format("/redfish/v1/Chassis/{}/TrustedComponents/{}/token",
                        chassisId, componentId);
    }
    else if (std::holds_alternative<std::string>(result))
    {
        std::string error = std::get<std::string>(result);
        messages::resourceErrorsDetectedFormatError(
            asyncResp->res,
            std::format("{} TrustedComponents {}", chassisId, componentId),
            error);
    }
    else
    {
        messages::internalError(asyncResp->res);
    }
}

static void tokenStatusCallback(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& chassisId, const std::string& componentId,
    const uint32_t& tokenId, const unified::action::Result& result)
{
    if (std::holds_alternative<NsmResult>(result))
    {
        const auto& [code, message] = std::get<NsmResult>(result);
        if (code == debugTokenUnsupportedNsmErrorCode)
        {
            messages::debugTokenUnsupported(
                asyncResp->res,
                std::format("{} TrustedComponents {}", chassisId, componentId));
        }
        else
        {
            messages::resourceErrorsDetectedFormatError(
                asyncResp->res,
                std::format("{} TrustedComponents {}", chassisId, componentId),
                message);
        }
    }
    else if (std::holds_alternative<unified::TokenStatus>(result))
    {
        auto tokenStatus = std::get<unified::TokenStatus>(result);
        if (tokenId >= tokenStatus.tokenTypesSubtypes.size())
        {
            messages::resourceNotFound(
                asyncResp->res,
                std::format("{} TrustedComponents {} Oem Nvidia DebugTokens",
                            chassisId, componentId),
                std::to_string(tokenId));
            return;
        }
        auto& resJson = asyncResp->res.jsonValue;
        unified::tokenStatusToJson(tokenStatus, tokenId, resJson);
        resJson["@odata.id"] = std::format(
            "/redfish/v1/Chassis/{}/TrustedComponents/{}/Oem/Nvidia/DebugTokens/{}",
            chassisId, componentId, tokenId);
        resJson["@odata.type"] = "#NvidiaDebugToken.v1_1_0.NvidiaDebugToken";
        resJson["Id"] = tokenId;
        resJson["Name"] =
            std::format("{} TrustedComponents {} Oem Nvidia DebugTokens {}",
                        chassisId, componentId, tokenId);
    }
    else
    {
        messages::internalError(asyncResp->res);
    }
}

static void tokenCollectionCallback(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& chassisId, const std::string& componentId,
    const unified::action::Result& result)
{
    if (std::holds_alternative<NsmResult>(result))
    {
        const auto& [code, message] = std::get<NsmResult>(result);
        if (code == debugTokenUnsupportedNsmErrorCode)
        {
            messages::debugTokenUnsupported(
                asyncResp->res,
                std::format("{} TrustedComponents {}", chassisId, componentId));
        }
        else
        {
            messages::resourceErrorsDetectedFormatError(
                asyncResp->res,
                std::format("{} TrustedComponents {}", chassisId, componentId),
                message);
        }
    }
    else if (std::holds_alternative<unified::TokenStatus>(result))
    {
        auto tokenStatus = std::get<unified::TokenStatus>(result);
        auto& resJson = asyncResp->res.jsonValue;

        resJson["@odata.id"] = std::format(
            "/redfish/v1/Chassis/{}/TrustedComponents/{}/Oem/Nvidia/DebugTokens",
            chassisId, componentId);
        resJson["@odata.type"] =
            "#NvidiaDebugTokenCollection.NvidiaDebugTokenCollection";
        resJson["Name"] =
            std::format("{} TrustedComponents {} Debug Tokens Collection",
                        chassisId, componentId);

        nlohmann::json& members = resJson["Members"];
        members = nlohmann::json::array();
        for (size_t i = 0; i < tokenStatus.tokenTypesSubtypes.size(); ++i)
        {
            nlohmann::json member;
            member["@odata.id"] = std::format(
                "/redfish/v1/Chassis/{}/TrustedComponents/{}/Oem/Nvidia/DebugTokens/{}",
                chassisId, componentId, i);
            members.push_back(std::move(member));
        }

        resJson["Members@odata.count"] = tokenStatus.tokenTypesSubtypes.size();
    }
    else
    {
        messages::internalError(asyncResp->res);
    }
}

inline void handleUnifiedTokenCollection(
    App& app, const crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& chassisId, const std::string& componentId)
{
    if (!redfish::setUpRedfishRoute(app, req, asyncResp))
    {
        return;
    }
    std::string targetChassisId =
        std::format("{}{}", PLATFORMDEVICEPREFIX, componentId);
    unified::action::Handler::startOperation(
        targetChassisId, unified::action::Operation::GetTokenStatus,
        std::monostate(),
        std::bind_front(&tokenCollectionCallback, asyncResp, chassisId,
                        componentId));
}

inline void handleUnifiedTokenStatus(
    App& app, const crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& chassisId, const std::string& componentId,
    const std::string& id)
{
    if (!redfish::setUpRedfishRoute(app, req, asyncResp))
    {
        return;
    }
    uint32_t tokenId = 0;
    try
    {
        tokenId = static_cast<uint32_t>(std::stoul(id));
    }
    catch (const std::invalid_argument&)
    {
        messages::resourceNotFound(
            asyncResp->res,
            std::format("{} TrustedComponents {} Oem Nvidia DebugTokens",
                        chassisId, componentId),
            id);
        return;
    }
    std::string targetChassisId =
        std::format("{}{}", PLATFORMDEVICEPREFIX, componentId);
    unified::action::Handler::startOperation(
        targetChassisId, unified::action::Operation::GetTokenStatus,
        std::monostate(),
        std::bind_front(&tokenStatusCallback, asyncResp, chassisId, componentId,
                        tokenId));
}

inline void handleUnifiedEraseToken(
    App& app, const crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& chassisId, const std::string& componentId)
{
    if (!redfish::setUpRedfishRoute(app, req, asyncResp))
    {
        return;
    }
    std::string eraseType;
    std::optional<std::string> tokenType;
    if (!redfish::json_util::readJsonAction(req, asyncResp->res, "EraseType",
                                            eraseType, "TokenType", tokenType))
    {
        return;
    }
    std::string valueToMap;
    if (eraseType == "TokenType")
    {
        if (!tokenType.has_value())
        {
            messages::actionParameterMissing(
                asyncResp->res, "NvidiaTrustedComponent.EraseToken",
                "TokenType");
            return;
        }
        valueToMap = tokenType.value();
    }
    else if (eraseType == "EraseAll" ||
             eraseType == "EraseAllAndRatchetCounterIncreased")
    {
        valueToMap = eraseType;
    }
    else
    {
        messages::actionParameterValueNotInList(
            asyncResp->res, eraseType, "EraseType",
            "NvidiaTrustedComponent.EraseToken");
        return;
    }
    std::string targetChassisId =
        std::format("{}{}", PLATFORMDEVICEPREFIX, componentId);
    uint32_t tokenTypeUint32 = unified::getTokenTypeAsUint32(valueToMap);
    if (tokenTypeUint32 == 0)
    {
        messages::actionParameterValueNotInList(asyncResp->res, valueToMap,
                                                "TokenType", "EraseToken");
        return;
    }
    unified::action::Handler::startOperation(
        targetChassisId, unified::action::Operation::EraseToken,
        tokenTypeUint32,
        std::bind_front(&eraseInstallCallback, asyncResp, chassisId,
                        componentId));
}

inline void handleUnifiedGenerateTokenRequest(
    App& app, const crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& chassisId, const std::string& componentId)
{
    if (!redfish::setUpRedfishRoute(app, req, asyncResp))
    {
        return;
    }
    std::string targetChassisId =
        std::format("{}{}", PLATFORMDEVICEPREFIX, componentId);
    unified::spdm_request::Handler::startOperation(
        targetChassisId, TokenType::DebugTokenRequest,
        std::bind_front(&generateTokenRequestCallback, asyncResp, chassisId,
                        componentId));
}

/**
 * @brief Extract and validate TokenFile from multipart form data
 *
 * This function parses the multipart form data to ensure the TokenFile
 * field is present. It uses the parseFormPartName utility to extract
 * form field names and validates that "TokenFile" exists.
 *
 * @param asyncResp Response object for error reporting
 * @param parser Multipart parser containing the parsed form data
 * @return true if TokenFile was found, false otherwise
 */
inline bool extractTokenFile(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const MultipartParser& parser)
{
    for (const FormPart& formpart : parser.mime_fields)
    {
        boost::beast::http::fields::const_iterator it =
            formpart.fields.find("Content-Disposition");
        if (it == formpart.fields.end())
        {
            BMCWEB_LOG_DEBUG("Couldn't find Content-Disposition");
            continue;
        }
        auto formFieldNameOpt = parseFormPartName(it);
        if (!formFieldNameOpt.has_value())
        {
            continue;
        }

        if (formFieldNameOpt.value() == "TokenFile")
        {
            return true;
        }
    }
    BMCWEB_LOG_ERROR("TokenFile form part is missing");
    messages::propertyMissing(asyncResp->res, "TokenFile");
    return false;
}

static void installTokenRedfishURLCallback(
    const crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& chassisId, const std::string& componentId,
    const std::string& targetChassisId, bool valid,
    [[maybe_unused]] const std::string& redfishURL)
{
    if (!valid)
    {
        BMCWEB_LOG_ERROR("Failed to get associated redfish URL for {}",
                         chassisId);
        messages::internalError(asyncResp->res);
        return;
    }
    std::string_view contentType = req.getHeaderValue("Content-Type");
    if (!contentType.starts_with("multipart/form-data"))
    {
        BMCWEB_LOG_DEBUG("Bad content type specified: {}", contentType);
        asyncResp->res.result(boost::beast::http::status::bad_request);
        return;
    }
    auto memFd = std::make_shared<MemoryFD>();
    MultipartParser parser(memFd->fd);
    ParserError ec = parser.parse(req);
    if (ec != ParserError::PARSER_SUCCESS)
    {
        BMCWEB_LOG_ERROR("MIME parse failed, ec: {}", static_cast<int>(ec));
        messages::internalError(asyncResp->res);
        return;
    }
    if (!extractTokenFile(asyncResp, parser))
    {
        return;
    }
    std::vector<uint8_t> fileData;
    try
    {
        memFd->read(fileData);
    }
    catch (const std::exception& e)
    {
        BMCWEB_LOG_ERROR("Failed to read token file: {}", e.what());
        messages::internalError(asyncResp->res);
        return;
    }

    constexpr size_t tokenFileHeaderSize = sizeof(TokenFileHeader);
    if (fileData.size() < tokenFileHeaderSize)
    {
        BMCWEB_LOG_ERROR("Token file too small: {} bytes", fileData.size());
        messages::internalError(asyncResp->res);
        return;
    }

    TokenFileHeader header{};
    std::memcpy(&header, fileData.data(), tokenFileHeaderSize);
    if (header.version != 0x02)
    {
        BMCWEB_LOG_ERROR("Invalid token file version: {}", header.version);
        messages::internalError(asyncResp->res);
        return;
    }
    if (header.type != static_cast<uint8_t>(TokenFileType::DebugToken))
    {
        BMCWEB_LOG_ERROR("Invalid token file type: {}", header.type);
        messages::internalError(asyncResp->res);
        return;
    }
    std::vector<uint8_t> tlvTokenData(
        fileData.begin() + static_cast<std::ptrdiff_t>(tokenFileHeaderSize),
        fileData.end());
    auto tlvMemFd = std::make_shared<MemoryFD>();
    try
    {
        tlvMemFd->write(tlvTokenData);
    }
    catch (const std::exception& e)
    {
        BMCWEB_LOG_ERROR("Failed to write TLV token data: {}", e.what());
        messages::internalError(asyncResp->res);
        return;
    }

    unified::action::Handler::startOperation(
        targetChassisId, unified::action::Operation::InstallToken, tlvMemFd,
        std::bind_front(&eraseInstallCallback, asyncResp, chassisId,
                        componentId));
}

static void installAssociationEndpointCallback(
    const crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& chassisId, const std::string& componentId,
    const std::string& targetChassisId, bool valid,
    const std::string& associationEndpoint)
{
    if (!valid)
    {
        BMCWEB_LOG_ERROR("Failed to get association endpoint for {}",
                         chassisId);
        messages::internalError(asyncResp->res);
        return;
    }
    chassis_utils::getRedfishURL(
        associationEndpoint,
        [&req, asyncResp, chassisId, componentId,
         targetChassisId](const bool& valid2, const std::string& redfishURL) {
            installTokenRedfishURLCallback(req, asyncResp, chassisId,
                                           componentId, targetChassisId, valid2,
                                           redfishURL);
        });
}

inline void handleUnifiedInstallToken(
    App& app, const crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& chassisId, const std::string& componentId)
{
    if (!redfish::setUpRedfishRoute(app, req, asyncResp))
    {
        return;
    }
    std::string targetChassisId =
        std::format("{}{}", PLATFORMDEVICEPREFIX, componentId);
    std::string inventoryPath = std::format(
        "/xyz/openbmc_project/inventory/system/chassis/{}/inventory",
        targetChassisId);
    chassis_utils::getAssociationEndpoint(
        inventoryPath,
        std::bind_front(&installAssociationEndpointCallback, std::cref(req),
                        asyncResp, chassisId, componentId, targetChassisId));
}

/**
 * @brief Handles GET request for EraseTokenActionInfo
 *
 * This function returns the ActionInfo resource that describes the parameters
 * for the EraseToken action.
 *
 * @param app Crow app
 * @param req HTTP request
 * @param asyncResp Response object
 * @param chassisId Chassis identifier
 * @param componentId Component identifier
 */
inline void handleEraseTokenActionInfo(
    crow::App& app, const crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& chassisId, const std::string& componentId)
{
    if (!redfish::setUpRedfishRoute(app, req, asyncResp))
    {
        return;
    }

    asyncResp->res.jsonValue["@odata.id"] = std::format(
        "/redfish/v1/Chassis/{}/TrustedComponents/{}/Oem/Nvidia/EraseTokenActionInfo",
        chassisId, componentId);
    asyncResp->res.jsonValue["@odata.type"] = "#ActionInfo.v1_2_0.ActionInfo";
    asyncResp->res.jsonValue["Id"] = "EraseTokenActionInfo";
    asyncResp->res.jsonValue["Name"] = "EraseTokens Action Info";

    nlohmann::json::array_t parameters;
    nlohmann::json::object_t parameter;
    parameter["Name"] = "EraseType";
    parameter["Required"] = true;
    parameter["DataType"] = "String";
    parameter["AllowableValues"] = {"EraseAll",
                                    "EraseAllAndRatchetCounterIncreased"};
    parameters.emplace_back(std::move(parameter));

    asyncResp->res.jsonValue["Parameters"] = std::move(parameters);
}

/**
 * @brief Handles GET request for token binary data
 *
 * This function retrieves the cached token data and returns it as an
 * octet-stream. The token data must have been generated previously via the
 * GenerateToken action.
 *
 * @param app Crow app
 * @param req HTTP request
 * @param asyncResp Response object
 * @param chassisId Chassis identifier
 * @param componentId Component identifier
 */
inline void handleTokenDataGet(
    crow::App& app, const crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& chassisId, const std::string& componentId)
{
    if (!redfish::setUpRedfishRoute(app, req, asyncResp))
    {
        return;
    }

    std::string cacheKey = std::format("{}:{}", chassisId, componentId);
    auto it = getTokenDataCache().find(cacheKey);

    if (it == getTokenDataCache().end())
    {
        messages::resourceNotFound(
            asyncResp->res, "TokenData",
            std::format("{}/{}", chassisId, componentId));
        return;
    }
    asyncResp->res.addHeader("Content-Type", "application/octet-stream");
    asyncResp->res.addHeader("Content-Transfer-Encoding", "Binary");
    std::string tokenFileString =
        std::string(it->second.begin(), it->second.end());
    asyncResp->res.write(std::move(tokenFileString));
}

} // namespace debug_token

inline void requestRoutesUnifiedDebugToken(App& app)
{
    using namespace debug_token;
    BMCWEB_ROUTE(app, "/redfish/v1/Chassis/<str>/TrustedComponents/<str>"
                      "/Oem/Nvidia/DebugTokens/")
        .privileges(redfish::privileges::getChassis)
        .methods(boost::beast::http::verb::get)(
            std::bind_front(handleUnifiedTokenCollection, std::ref(app)));
    BMCWEB_ROUTE(app, "/redfish/v1/Chassis/<str>/TrustedComponents/<str>"
                      "/Oem/Nvidia/DebugTokens/<str>/")
        .privileges(redfish::privileges::getChassis)
        .methods(boost::beast::http::verb::get)(
            std::bind_front(handleUnifiedTokenStatus, std::ref(app)));
    BMCWEB_ROUTE(app, "/redfish/v1/Chassis/<str>/TrustedComponents/<str>"
                      "/Oem/Nvidia/EraseTokenActionInfo")
        .privileges(redfish::privileges::getChassis)
        .methods(boost::beast::http::verb::get)(
            std::bind_front(handleEraseTokenActionInfo, std::ref(app)));
    BMCWEB_ROUTE(app, "/redfish/v1/Chassis/<str>/TrustedComponents/<str>"
                      "/token")
        .privileges(redfish::privileges::getChassis)
        .methods(boost::beast::http::verb::get)(
            std::bind_front(handleTokenDataGet, std::ref(app)));
    BMCWEB_ROUTE(app, "/redfish/v1/Chassis/<str>/TrustedComponents/<str>"
                      "/Actions/Oem/NvidiaTrustedComponent.EraseToken")
        .privileges(redfish::privileges::postChassis)
        .methods(boost::beast::http::verb::post)(
            std::bind_front(handleUnifiedEraseToken, std::ref(app)));
    BMCWEB_ROUTE(app, "/redfish/v1/Chassis/<str>/TrustedComponents/<str>"
                      "/Actions/Oem/NvidiaTrustedComponent.GenerateToken")
        .privileges(redfish::privileges::postChassis)
        .methods(boost::beast::http::verb::post)(
            std::bind_front(handleUnifiedGenerateTokenRequest, std::ref(app)));
    BMCWEB_ROUTE(app, "/redfish/v1/Chassis/<str>/TrustedComponents/<str>"
                      "/install-token")
        .privileges(redfish::privileges::postChassis)
        .methods(boost::beast::http::verb::post)(
            std::bind_front(handleUnifiedInstallToken, std::ref(app)));
}

} // namespace redfish
