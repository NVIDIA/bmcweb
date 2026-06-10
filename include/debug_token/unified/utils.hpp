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

#include <nlohmann/json.hpp>

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <tuple>
#include <vector>

namespace redfish::debug_token::unified
{

// Token type mapping: Redfish name <-> D-Bus enum string
static constexpr std::array<std::pair<std::string_view, std::string_view>, 21>
    tokenTypeMap = {{
        {"FRC", "com.nvidia.DebugToken.Common.Types.FRC"},
        {"CRCS", "com.nvidia.DebugToken.Common.Types.CRCS"},
        {"CRDT", "com.nvidia.DebugToken.Common.Types.CRDT"},
        {"DebugFirmwareRunning",
         "com.nvidia.DebugToken.Common.Types.DebugFirmwareRunning"},
        {"DebugFirmwareUnlock",
         "com.nvidia.DebugToken.Common.Types.DebugFirmwareUnlock"},
        {"OTPDumpEnable", "com.nvidia.DebugToken.Common.Types.OTPDumpEnable"},
        {"JtagUnlock", "com.nvidia.DebugToken.Common.Types.JtagUnlock"},
        {"HardwareUnlock", "com.nvidia.DebugToken.Common.Types.HardwareUnlock"},
        {"RuntimeDebugUnlock",
         "com.nvidia.DebugToken.Common.Types.RuntimeDebugUnlock"},
        {"FeatureUnlock", "com.nvidia.DebugToken.Common.Types.FeatureUnlock"},
        {"MTDT", "com.nvidia.DebugToken.Common.Types.MTDT"},
        {"CcplexArmJtagDebugCont",
         "com.nvidia.DebugToken.Common.Types.CcplexArmJtagDebugCont"},
        {"NVJtagControl", "com.nvidia.DebugToken.Common.Types.NVJtagControl"},
        {"DiagnosticBoot", "com.nvidia.DebugToken.Common.Types.DiagnosticBoot"},
        {"BpmpFirmwareDebugFS",
         "com.nvidia.DebugToken.Common.Types.BpmpFirmwareDebugFS"},
        {"FirmwareDebugKnobs",
         "com.nvidia.DebugToken.Common.Types.FwDebugKnobs"},
        {"FirewallLifting",
         "com.nvidia.DebugToken.Common.Types.FirewallLifting"},
        {"Verbosity", "com.nvidia.DebugToken.Common.Types.Verbosity"},
        {"RAS", "com.nvidia.DebugToken.Common.Types.RAS"},
        {"SMADebugCapability",
         "com.nvidia.DebugToken.Common.Types.SMADebugCapability"},
        {"CpldDebugCapability",
         "com.nvidia.DebugToken.Common.Types.CpldDebugCapability"},
    }};

// Token subtype mapping: Redfish name <-> D-Bus enum string
static constexpr std::array<std::pair<std::string_view, std::string_view>, 93>
    tokenSubtypeMap = {
        {{"OOBHub", "com.nvidia.DebugToken.Common.SubTypes.OOBHub"},
         {"RAS", "com.nvidia.DebugToken.Common.SubTypes.RAS"},
         {"Pcore", "com.nvidia.DebugToken.Common.SubTypes.Pcore"},
         {"PscFirmware", "com.nvidia.DebugToken.Common.SubTypes.PscFirmware"},
         {"MB1", "com.nvidia.DebugToken.Common.SubTypes.MB1"},
         {"MB2", "com.nvidia.DebugToken.Common.SubTypes.MB2"},
         {"BpmpFirmware", "com.nvidia.DebugToken.Common.SubTypes.BpmpFirmware"},
         {"BpmpIst", "com.nvidia.DebugToken.Common.SubTypes.BpmpIst"},
         {"BpmpDiag", "com.nvidia.DebugToken.Common.SubTypes.BpmpDiag"},
         {"MSEQ", "com.nvidia.DebugToken.Common.SubTypes.MSEQ"},
         {"C2C", "com.nvidia.DebugToken.Common.SubTypes.C2C"},
         {"ATF", "com.nvidia.DebugToken.Common.SubTypes.ATF"},
         {"RMM", "com.nvidia.DebugToken.Common.SubTypes.RMM"},
         {"PscFmc", "com.nvidia.DebugToken.Common.SubTypes.PscFmc"},
         {"SensitiveLoggingEnable",
          "com.nvidia.DebugToken.Common.SubTypes.EnableSensitiveLogging"},
         {"DBGN", "com.nvidia.DebugToken.Common.SubTypes.Dbgen"},
         {"NIDEN", "com.nvidia.DebugToken.Common.SubTypes.Niden"},
         {"SPIDEN", "com.nvidia.DebugToken.Common.SubTypes.Spiden"},
         {"SPNIDEN", "com.nvidia.DebugToken.Common.SubTypes.Spniden"},
         {"JtagEnable", "com.nvidia.DebugToken.Common.SubTypes.JtagEnable"},
         {"DeviceenApbap",
          "com.nvidia.DebugToken.Common.SubTypes.DeviceenApbap"},
         {"DeviceenAxiap",
          "com.nvidia.DebugToken.Common.SubTypes.DeviceenAxiap"},
         {"RLPIDEN", "com.nvidia.DebugToken.Common.SubTypes.Rlpiden"},
         {"RTPIDEN", "com.nvidia.DebugToken.Common.SubTypes.Rtpiden"},
         {"NvcpuUcfApb2ctlpDBGN",
          "com.nvidia.DebugToken.Common.SubTypes.NvcpuUcfApb2ctlpDbgen"},
         {"NvcpuUcfElaEnable",
          "com.nvidia.DebugToken.Common.SubTypes.NvcpuUcfElaEn"},
         {"EcProbesEnable", "com.nvidia.DebugToken.Common.SubTypes.EcProbesEn"},
         {"CswpEnable", "com.nvidia.DebugToken.Common.SubTypes.EnableCswp"},
         {"DftAccessAllowed",
          "com.nvidia.DebugToken.Common.SubTypes.DftAccessAllowed"},
         {"BpmpDiagnosticBoot",
          "com.nvidia.DebugToken.Common.SubTypes.BpmpDiagnosticBoot"},
         {"CPUDiagnosticBoot",
          "com.nvidia.DebugToken.Common.SubTypes.CPUDiagnosticBoot"},
         {"BpmpFirmwareDebugfs",
          "com.nvidia.DebugToken.Common.SubTypes.BpmpFwDebugfs"},
         {"MseqEnableSbeReporting",
          "com.nvidia.DebugToken.Common.SubTypes.MseqEnableSbeReporting"},
         {"GroupB", "com.nvidia.DebugToken.Common.SubTypes.GroupB"},
         {"GroupC", "com.nvidia.DebugToken.Common.SubTypes.GroupC"},
         {"RasTest", "com.nvidia.DebugToken.Common.SubTypes.RasTest"},
         {"PowerFailI2cDebug",
          "com.nvidia.DebugToken.Common.SubTypes.PowerFailI2cDebug"},
         {"CpldDebugEnable",
          "com.nvidia.DebugToken.Common.SubTypes.CpldDebugEnable"},
         {"SMA", "com.nvidia.DebugToken.Common.SubTypes.MCU"},
         {"CPLD", "com.nvidia.DebugToken.Common.SubTypes.CPLD"},
         {"LPX", "com.nvidia.DebugToken.Common.SubTypes.LPX"},
         {"VBIOS", "com.nvidia.DebugToken.Common.SubTypes.VBIOS"},
         {"FSPRT", "com.nvidia.DebugToken.Common.SubTypes.FSPRT"},
         {"OOBHubRT", "com.nvidia.DebugToken.Common.SubTypes.OOBHubRT"},
         {"NetIR", "com.nvidia.DebugToken.Common.SubTypes.NetIR"},
         {"PCIe", "com.nvidia.DebugToken.Common.SubTypes.PCIe"},
         {"FBFALCON", "com.nvidia.DebugToken.Common.SubTypes.FBFALCON"},
         {"CTCHBI", "com.nvidia.DebugToken.Common.SubTypes.CTCHBI"},
         {"BuildInfo", "com.nvidia.DebugToken.Common.SubTypes.BuildInfo"},
         {"DataTables", "com.nvidia.DebugToken.Common.SubTypes.DataTables"},
         {"PreltssmDevinit",
          "com.nvidia.DebugToken.Common.SubTypes.PreltssmDevinit"},
         {"PostltssmDevinit",
          "com.nvidia.DebugToken.Common.SubTypes.PostltssmDevinit"},
         {"PrimaryImage", "com.nvidia.DebugToken.Common.SubTypes.PrimaryImage"},
         {"UEFIx86", "com.nvidia.DebugToken.Common.SubTypes.UEFIx86"},
         {"UEFIArm", "com.nvidia.DebugToken.Common.SubTypes.UEFIArm"},
         {"Duck", "com.nvidia.DebugToken.Common.SubTypes.Duck"},
         {"InforomRO", "com.nvidia.DebugToken.Common.SubTypes.InforomRO"},
         {"DevInit", "com.nvidia.DebugToken.Common.SubTypes.DevInit"},
         {"ExtensionImage",
          "com.nvidia.DebugToken.Common.SubTypes.ExtensionImage"},
         {"MSE", "com.nvidia.DebugToken.Common.SubTypes.MSE"},
         {"RomDirectory", "com.nvidia.DebugToken.Common.SubTypes.RomDirectory"},
         {"GspRm", "com.nvidia.DebugToken.Common.SubTypes.GspRm"},
         {"MemQual", "com.nvidia.DebugToken.Common.SubTypes.MemQual"},
         {"GlobalFunctionalDebug",
          "com.nvidia.DebugToken.Common.SubTypes.GlobalFunctionalDebug"},
         {"SpeedoKappaIddq",
          "com.nvidia.DebugToken.Common.SubTypes.SpeedoKappaIddq"},
         {"ECCEnable", "com.nvidia.DebugToken.Common.SubTypes.ECCEnable"},
         {"ReducedGlobalFunctionalDebug",
          "com.nvidia.DebugToken.Common.SubTypes.ReducedGlobalFunctionalDebug"},
         {"ECCUnlock", "com.nvidia.DebugToken.Common.SubTypes.ECCUnlock"},
         {"Bar0DecouplerUnlock",
          "com.nvidia.DebugToken.Common.SubTypes.Bar0DecouplerUnlock"},
         {"I2ccAccess", "com.nvidia.DebugToken.Common.SubTypes.I2ccAccess"},
         {"ECCPoisonDisable",
          "com.nvidia.DebugToken.Common.SubTypes.ECCPoisonDisable"},
         {"NvlinkGlobalHulk",
          "com.nvidia.DebugToken.Common.SubTypes.NvlinkGlobalHulk"},
         {"LowerRomProtection",
          "com.nvidia.DebugToken.Common.SubTypes.LowerRomProtection"},
         {"RecreateInforomRW",
          "com.nvidia.DebugToken.Common.SubTypes.RecreateInforomRW"},
         {"PrcKnobEn", "com.nvidia.DebugToken.Common.SubTypes.PrcKnobEn"},
         {"Nuke", "com.nvidia.DebugToken.Common.SubTypes.Nuke"},
         {"ArchFunctionalTests",
          "com.nvidia.DebugToken.Common.SubTypes.ArchFunctionalTests"},
         {"SsgDebug", "com.nvidia.DebugToken.Common.SubTypes.SsgDebug"},
         {"CxlDebug", "com.nvidia.DebugToken.Common.SubTypes.CxlDebug"},
         {"VideDebug", "com.nvidia.DebugToken.Common.SubTypes.VideDebug"},
         {"GlobalFunctionalDebugMinimal",
          "com.nvidia.DebugToken.Common.SubTypes.GlobalFunctionalDebugMinimal"},
         {"IstDebug", "com.nvidia.DebugToken.Common.SubTypes.IstDebug"},
         {"PxucGlobalHulk",
          "com.nvidia.DebugToken.Common.SubTypes.PxucGlobalHulk"},
         {"TargetMaskUnlock",
          "com.nvidia.DebugToken.Common.SubTypes.TargetMaskUnlock"},
         {"I2cVregUnlock",
          "com.nvidia.DebugToken.Common.SubTypes.I2cVregUnlock"},
         {"NvlinkErrorInjectionEnable",
          "com.nvidia.DebugToken.Common.SubTypes.NvlinkErrorInjectionEnable"},
         {"NvlinkDebugApiEnable",
          "com.nvidia.DebugToken.Common.SubTypes.NvlinkDebugApiEnable"},
         {"NetIRUnitTestEnable",
          "com.nvidia.DebugToken.Common.SubTypes.NetIRUnitTestEnable"},
         {"NvlinkMissionDebugGmt",
          "com.nvidia.DebugToken.Common.SubTypes.NvlinkMissionDebugGmt"},
         {"PxucDebugApiEnable",
          "com.nvidia.DebugToken.Common.SubTypes.PxucDebugApiEnable"},
         {"PxucErrorInjectionEnable",
          "com.nvidia.DebugToken.Common.SubTypes.PxucErrorInjectionEnable"},
         {"PxucMissionDebugGmt",
          "com.nvidia.DebugToken.Common.SubTypes.PxucMissionDebugGmt"},
         {"GspTaskDebugEnable",
          "com.nvidia.DebugToken.Common.SubTypes.EnableGspTaskDebug"}}};

/**
 * @brief Represents the status of NSM debug tokens
 *
 * This struct encapsulates the installation and processing status of debug
 * tokens, along with a list of token type and subtype pairs that are currently
 * active.
 */
struct TokenStatus
{
    /**
     * @brief Constructs an TokenStatus object
     *
     * @param installationStatus Whether the token is installed
     * @param processingStatus Whether the token is being processed
     * @param tokenTypeSubtypeList Vector of token type string/subtype string
     * array pairs
     * @param deviceID Device ID associated with the token
     */
    explicit TokenStatus(
        bool installationStatus, bool processingStatus,
        const std::vector<std::tuple<std::string, std::vector<std::string>>>&
            tokenTypeSubtypeList,
        const std::string& deviceID) :
        installation(installationStatus), processing(processingStatus),
        tokenTypesSubtypes(tokenTypeSubtypeList), tokenDeviceID(deviceID)
    {}

    bool installation;
    bool processing;
    std::vector<std::tuple<std::string, std::vector<std::string>>>
        tokenTypesSubtypes;
    std::string tokenDeviceID;
};

/**
 * @brief Maps a Redfish erase type string to its D-Bus enum representation
 *
 * @param eraseType The Redfish erase type string
 * @return std::optional<std::string> The full D-Bus enum string, or
 * std::nullopt if invalid
 */
inline std::optional<std::string> mapEraseTypeToDbusEnum(
    const std::string& eraseType)
{
    static constexpr std::array<std::pair<std::string_view, std::string_view>,
                                3>
        eraseTypeMap = {{
            {"TokenType", "com.nvidia.DebugToken.Action.EraseType.TokenType"},
            {"EraseAll", "com.nvidia.DebugToken.Action.EraseType.EraseAll"},
            {"EraseAllAndRatchetCounterIncreased",
             "com.nvidia.DebugToken.Action.EraseType."
             "EraseAllAndRatchetCounterIncreased"},
        }};

    for (const auto& [key, value] : eraseTypeMap)
    {
        if (key == eraseType)
        {
            return std::string(value);
        }
    }
    return std::nullopt;
}

/**
 * @brief Maps a Redfish token type string to its D-Bus enum representation
 *
 * @param tokenType The Redfish token type string
 * @return std::optional<std::string> The full D-Bus enum string, or
 * std::nullopt if invalid
 */
inline std::optional<std::string> mapTokenTypeToDbusEnum(
    const std::string& tokenType)
{
    for (const auto& [key, value] : tokenTypeMap)
    {
        if (key == tokenType)
        {
            return std::string(value);
        }
    }
    return std::nullopt;
}

/**
 * @brief Maps a D-Bus token type enum string to its Redfish representation
 *
 * @param dbusEnum The full D-Bus enum string
 * @return std::optional<std::string> The Redfish name, or std::nullopt if
 * invalid
 */
inline std::optional<std::string> mapDbusEnumToTokenType(
    const std::string& dbusEnum)
{
    for (const auto& [key, value] : tokenTypeMap)
    {
        if (value == dbusEnum)
        {
            return std::string(key);
        }
    }
    return std::nullopt;
}

/**
 * @brief Maps a Redfish token subtype string to its D-Bus enum representation
 *
 * @param tokenSubtype The Redfish token subtype string
 * @return std::optional<std::string> The full D-Bus enum string, or
 * std::nullopt if invalid
 */
inline std::optional<std::string> mapTokenSubtypeToDbusEnum(
    const std::string& tokenSubtype)
{
    for (const auto& [key, value] : tokenSubtypeMap)
    {
        if (key == tokenSubtype)
        {
            return std::string(value);
        }
    }
    return std::nullopt;
}

/**
 * @brief Maps a D-Bus token subtype enum string to its Redfish representation
 *
 * @param dbusEnum The full D-Bus enum string
 * @return std::optional<std::string> The Redfish name, or std::nullopt if
 * invalid
 */
inline std::optional<std::string> mapDbusEnumToTokenSubtype(
    const std::string& dbusEnum)
{
    for (const auto& [key, value] : tokenSubtypeMap)
    {
        if (value == dbusEnum)
        {
            return std::string(key);
        }
    }
    return std::nullopt;
}

/**
 * @brief Gets the list of allowable individual token types (excluding special
 * erase types)
 *
 * Returns token types that can be used for individual token operations,
 * excluding None and the special erase-all types.
 * This list is from the Redfish schema TokenType enum.
 *
 * @return std::vector<std::string> List of allowable token type strings
 */
inline std::vector<std::string> getAllowableTokenTypes()
{
    std::vector<std::string> types;
    types.reserve(tokenTypeMap.size());
    for (const auto& [key, value] : tokenTypeMap)
    {
        types.emplace_back(key);
    }
    return types;
}

/**
 * @brief Converts TokenStatus to JSON format. Used in aggregate token status
 *
 * @param status The NSM token status to convert
 * @param j The JSON object to populate with the status information
 */
inline void tokenStatusToJson(const TokenStatus& status, nlohmann::json& j)
{
    j["ProcessingStatus"] = status.processing ? "Processed" : "NotProcessed";
    j["TokenInstalled"] = status.installation;
    j["DeviceID"] = status.tokenDeviceID;
    nlohmann::json tokens = nlohmann::json::array();

    for (const auto& [tokenType, tokenSubtypes] : status.tokenTypesSubtypes)
    {
        std::optional<std::string> redfishTokenType =
            mapDbusEnumToTokenType(tokenType);
        if (!redfishTokenType.has_value())
        {
            continue;
        }

        nlohmann::json token;
        token["TokenType"] = redfishTokenType.value();

        nlohmann::json subtypesArray = nlohmann::json::array();
        for (const auto& subtype : tokenSubtypes)
        {
            std::optional<std::string> redfishSubtype =
                mapDbusEnumToTokenSubtype(subtype);
            if (redfishSubtype.has_value())
            {
                subtypesArray.push_back(redfishSubtype.value());
            }
        }
        token["TokenSubTypes"] = std::move(subtypesArray);

        tokens.push_back(std::move(token));
    }
    j["Tokens"] = std::move(tokens);
}

/**
 * @brief Converts a specific token from TokenStatus to JSON format
 *
 * Token types and subtypes are D-Bus enum strings that are validated and
 * converted to Redfish names. Invalid enum values are logged and skipped.
 *
 * @param status The token status containing the token information
 * @param tokenIndex The index of the specific token to convert
 * @param j The JSON object to populate with the token information
 */
inline void tokenStatusToJson(const TokenStatus& status, size_t tokenIndex,
                              nlohmann::json& j)
{
    if (tokenIndex >= status.tokenTypesSubtypes.size())
    {
        return;
    }

    const auto& [tokenType, tokenSubtypes] =
        status.tokenTypesSubtypes[tokenIndex];
    std::optional<std::string> redfishTokenType =
        mapDbusEnumToTokenType(tokenType);
    if (!redfishTokenType.has_value())
    {
        return;
    }
    j["TokenType"] = redfishTokenType.value();

    nlohmann::json subtypesArray = nlohmann::json::array();
    for (const auto& subtype : tokenSubtypes)
    {
        std::optional<std::string> redfishSubtype =
            mapDbusEnumToTokenSubtype(subtype);
        if (redfishSubtype.has_value())
        {
            subtypesArray.push_back(redfishSubtype.value());
        }
    }
    j["TokenSubTypes"] = std::move(subtypesArray);
    // Deprecated, keeping for backward compatibility
    j["AdditionalInfo"] = "None";
}

} // namespace redfish::debug_token::unified
