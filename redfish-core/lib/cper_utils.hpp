/*
 * SPDX-FileCopyrightText: Copyright (c) 2024 NVIDIA CORPORATION & AFFILIATES.
 * All rights reserved. SPDX-License-Identifier: Apache-2.0
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

#include <boost/url/format.hpp>

#include <map>
#include <optional>
#include <string>
#include <vector>

namespace redfish
{

inline std::string capitalizeProp(const std::string& key)
{
    std::string ret = std::string(key);
    if (ret.length() && isalpha(ret[0]))
    {
        ret[0] = static_cast<char>(toupper(ret[0]));
    }
    return ret;
}

template <class UnaryFunction>
inline void jsonIterate(nlohmann::json& jOut, const nlohmann::json& jIn,
                        UnaryFunction changeProp, bool debug = 0)
{
    if (!jIn.is_structured())
    {
        jOut = jIn;
        return;
    }

    for (auto it = jIn.begin(); it != jIn.end(); ++it)
    {
        std::string kstr;

        kstr = changeProp(it.key());

        // Filter unwanted properties
        if (kstr == "ErrorInformation")
        {
            jsonIterate(jOut, it.value(), changeProp);
            continue;
        }

        if (it->is_structured())
        {
            if (it.value().is_object())
            {
                jsonIterate(jOut[kstr], it.value(), changeProp);
            }
            else if (it.value().is_array())
            {
                jOut[kstr] = nlohmann::json::array();
                for (auto& arr_i : *it)
                {
                    jOut[kstr].push_back(nlohmann::json());
                    jsonIterate(jOut[kstr].back(), arr_i, changeProp);
                }
            }
            else
            {
                BMCWEB_LOG_WARNING("Unknown JSON structured type");
            }
        }
        else
        {
            jOut[kstr] = it.value();
        }
    }
    if (debug)
    {
        BMCWEB_LOG_DEBUG("jIn:\n", jIn.dump());
        BMCWEB_LOG_DEBUG("jOut:\n", jOut.dump());
        BMCWEB_LOG_DEBUG(
            "---------------------------------------------------------------------\n\n\n\n");
    }
}

inline int severityToStr(const std::string& code, std::string& out)
{
    const std::map<std::string, std::string> code_map = {
        {"0", "Warning"}, {"1", "Critical"}, {"2", "OK"}, {"3", "Warning"}};
    const auto it = code_map.find(code);
    if (it != code_map.end())
    {
        out = it->second;
        return 0;
    }
    return 1;
}

inline boost::urls::url handleMemProcessorOrigin(const nlohmann::json& mainCper)
{
    const auto& nodeIt = mainCper.find("Node");
    if (nodeIt == mainCper.end())
    {
        BMCWEB_LOG_ERROR("Node property not found");
        return boost::urls::url();
    }
    uint64_t node = *(nodeIt->get_ptr<const uint64_t*>());

    return boost::urls::format("/redfish/v1/Systems/{}/Processors/CPU_{}",
                               BMCWEB_REDFISH_SYSTEM_URI_NAME, node);
}

inline boost::urls::url handleNvProcessorOrigin(const nlohmann::json& mainCper)
{
    const auto& sockIt = mainCper.find("Socket");
    if (sockIt == mainCper.end())
    {
        BMCWEB_LOG_ERROR("Socket property not found");
        return boost::urls::url();
    }
    uint64_t sock = *(sockIt->get_ptr<const uint64_t*>());

    return boost::urls::format("/redfish/v1/Systems/{}/Processors/CPU_{}",
                               BMCWEB_REDFISH_SYSTEM_URI_NAME, sock);
}

inline boost::urls::url handleArmProcessorOrigin(const nlohmann::json& mainCper)
{
    boost::urls::url origin;
    const auto& mpIt = mainCper.find("Affinity3");
    if (mpIt == mainCper.end())
    {
        BMCWEB_LOG_ERROR("Aff3 not found");
        return boost::urls::url();
    }

    // mpidrEli bits 32:39 denote aff3/socket num
    uint64_t sock = *(mpIt->get_ptr<const uint64_t*>());

    return boost::urls::format("/redfish/v1/Systems/{}/Processors/CPU_{}",
                               BMCWEB_REDFISH_SYSTEM_URI_NAME, sock);
}

inline std::optional<boost::urls::url>
    handleOriginCondition(const nlohmann::json& nvCper)
{
    boost::urls::url origin;
    const auto& aType = nvCper.find("ArmProcessor");
    if (aType != nvCper.end())
    {
        BMCWEB_LOG_DEBUG("ArmProcessor found");
        origin = handleArmProcessorOrigin(*aType);
        if (origin.empty())
        {
            return std::nullopt;
        }
        return origin;
    }
    const auto& nvType = nvCper.find("Nvidia");
    if (nvType != nvCper.end())
    {
        BMCWEB_LOG_DEBUG("Nvidia type found");
        origin = handleNvProcessorOrigin(*nvType);
        if (origin.empty())
        {
            return std::nullopt;
        }
        return origin;
    }
    const auto& memType = nvCper.find("Memory");
    if (memType != nvCper.end())
    {
        BMCWEB_LOG_DEBUG("Memory type found");
        origin = handleMemProcessorOrigin(*memType);
        if (origin.empty())
        {
            return std::nullopt;
        }
        return std::nullopt;
    }

    BMCWEB_LOG_ERROR("OriginOfCondition not supported");
    return std::nullopt;
}

inline void parseAdditionalDataForCPER(
    nlohmann::json::object_t& entry,
    [[maybe_unused]] const nlohmann::json::object_t& oem,
    const AdditionalData& additional, std::string& originStr)
{
    const auto& type = additional.find("diagnosticDataType");
    if (additional.end() == type ||
        ("CPER" != type->second && "CPERSection" != type->second))
    {
        return;
    }

    BMCWEB_LOG_DEBUG("Got {}", type->second);

    nlohmann::json jOut;

    const auto& notifT = additional.find("notificationType");
    if (additional.end() == notifT)
    {
        BMCWEB_LOG_ERROR("notificationType property not found in CPER log");
        return;
    }
    BMCWEB_LOG_DEBUG("Adding notificationType");
    jOut["CPER"]["NotificationType"] = notifT->second;

    const auto& sevCode = additional.find("cperSeverityCode");
    if (additional.end() == sevCode)
    {
        BMCWEB_LOG_ERROR("severity code property not found in CPER log");
        return;
    }

    std::string code_val;
    if (!severityToStr(sevCode->second, code_val))
    {
        BMCWEB_LOG_DEBUG("Adding severity code");
        jOut["Severity"] = code_val;
    }

    const auto& diagData = additional.find("diagnosticData");
    if (additional.end() == diagData)
    {
        BMCWEB_LOG_ERROR("diagnosticData property not found in CPER log");
        return;
    }

    BMCWEB_LOG_DEBUG("Adding diagnosticData");
    jOut["DiagnosticData"] = diagData->second;

    const auto& secT = additional.find("sectionType");
    if (additional.end() == secT)
    {
        BMCWEB_LOG_WARNING("sectionType property not found in CPER log");
    }
    else
    {
        jOut["CPER"]["SectionType"] = secT->second;
    }

    nlohmann::json cperData;
    const auto& jDiag = additional.find("jsonDiagnosticData");
    if (additional.end() == jDiag)
    {
        BMCWEB_LOG_ERROR("jsonDiagnosticData property not found in CPER log");
    }
    else
    {
        cperData = nlohmann::json::parse(jDiag->second, nullptr, false);
        if (cperData.is_discarded())
        {
            BMCWEB_LOG_ERROR("Could not parse CPER jsonDiagnosticData");
            return;
        }
    }

    if (cperData.find("sections") == cperData.end())
    {
        BMCWEB_LOG_ERROR("Sections property not found in CPER log");
        return;
    }

    const nlohmann::json::array_t* sections =
        cperData["sections"].get_ptr<const nlohmann::json::array_t*>();
    if (sections == nullptr)
    {
        BMCWEB_LOG_ERROR("sections property in CPER is not an array");
        return;
    }

    // Iterate over Sections:
    for (const auto& section : *sections)
    {
        jsonIterate(jOut["CPER"]["Oem"]["Nvidia"], section, capitalizeProp);
        // We only care about the first section
        break;
    }

    // Expect timestamp to be formatted as
    // ISO8061 string
    const auto& cperTime = additional.find("timestamp");
    if (additional.end() == cperTime)
    {
        // Don't exit here, use HMC time by default
        BMCWEB_LOG_ERROR("timestamp property not found in CPER log");
    }
    else
    {
        jOut["Created"] = cperTime->second;
    }

    jOut["DiagnosticDataType"] = "CPERSection";
    jOut["MessageId"] = "Platform.1.0.PlatformError";

    // NVIDIA
    jOut["CPER"]["Oem"]["Nvidia"]["@odata.type"] =
        "#NvidiaCPER.v1_0_0.NvidiaCPER";

    // OriginOfCondition
    if (originStr.empty())
    {
        std::optional<boost::urls::url> origin =
            handleOriginCondition(jOut["CPER"]["Oem"]["Nvidia"]);

        if (!origin)
        {
            BMCWEB_LOG_ERROR(
                "OriginOfCondition RF property not found in CPER log");
        }
        else
        {
            jOut["Links"]["OriginOfCondition"]["@odata.id"] = *origin;
            // Eventing needs ooc as a string
            // maintain support for sendEventWithOOC()
            originStr = std::string((*origin).buffer());
        }
    }

    entry = jOut;

    BMCWEB_LOG_DEBUG("Done {}", type->second);
}

} // namespace redfish
