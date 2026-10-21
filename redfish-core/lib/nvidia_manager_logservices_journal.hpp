/*
 * SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION &
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

#include "app.hpp"
#include "async_resp.hpp"
#include "bmcweb_config.h"
#include "dbus_singleton.hpp"
#include "dbus_utility.hpp"
#include "error_messages.hpp"
#include "generated/enums/log_service.hpp"
#include "http_request.hpp"
#include "logging.hpp"
#include "query.hpp"
#include "registries/privilege_registry.hpp"
#include "utils/dbus_utils.hpp"
#include "utils/json_utils.hpp"

#include <boost/beast/http/verb.hpp>
#include <nlohmann/json.hpp>

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace redfish
{

constexpr const char* journalConfigService =
    "xyz.openbmc_project.Journal.Config";
constexpr const char* journalConfigPath =
    "/xyz/openbmc_project/logging/journal";
constexpr const char* journalConfigIface =
    "xyz.openbmc_project.Logging.Journal";
constexpr const char* entryLevelPrefix =
    "xyz.openbmc_project.Logging.Entry.Level.";

/**
 * @brief Redfish AllowableValues for SyslogFilters LowestSeverity.
 *
 * Includes All. Emergency and Alert are valid PATCH values.
 */
inline nlohmann::json::array_t journalLowestSeverityAllowableValues()
{
    return nlohmann::json::array_t{
        "Emergency",
        "Alert",
        "Critical",
        "Error",
        "Warning",
        "Notice",
        "Informational",
        "Debug",
        "All",
    };
}

/**
 * @brief Parse a Redfish SyslogSeverity string.
 *
 * Exact name match. Unknown strings are nullopt.
 *
 * @param severity Redfish SyslogSeverity string.
 */
inline std::optional<log_service::SyslogSeverity> parseRedfishSyslogSeverity(
    std::string_view severity)
{
    log_service::SyslogSeverity parsed = log_service::SyslogSeverity::Invalid;
    from_json(nlohmann::json(std::string(severity)), parsed);
    if (parsed == log_service::SyslogSeverity::Invalid)
    {
        return std::nullopt;
    }
    return parsed;
}

/**
 * @brief Map D-Bus Entry.Level to Redfish SyslogSeverity.
 *
 * Strip the Entry.Level prefix, then exact name match.
 *
 * @param entryLevel D-Bus Entry.Level string.
 */
inline std::optional<log_service::SyslogSeverity> toRedfishSyslogSeverity(
    std::string_view entryLevel)
{
    std::string_view leaf = entryLevel;
    std::string_view prefix(entryLevelPrefix);
    if (leaf.starts_with(prefix))
    {
        leaf.remove_prefix(prefix.size());
    }
    return parseRedfishSyslogSeverity(leaf);
}

/**
 * @brief Map Redfish SyslogSeverity to D-Bus Entry.Level.
 *
 * All (and empty SyslogFilters) map to Debug: journald has no "all";
 * debug is the most inclusive MaxLevelStore. A later GET returns Debug,
 * not All.
 *
 * @param severity Redfish SyslogSeverity.
 */
inline std::optional<std::string> toDbusEntryLevel(
    log_service::SyslogSeverity severity)
{
    if (severity == log_service::SyslogSeverity::Invalid)
    {
        return std::nullopt;
    }
    if (severity == log_service::SyslogSeverity::All)
    {
        severity = log_service::SyslogSeverity::Debug;
    }
    const nlohmann::json name(severity);
    const std::string* leaf = name.get_ptr<const std::string*>();
    if (leaf == nullptr)
    {
        return std::nullopt;
    }
    return std::string(entryLevelPrefix) + *leaf;
}

/**
 * @brief Fill LogService SyslogFilters from Journal.Config LowestSeverity.
 *
 * Any Get failure omits SyslogFilters and does not fail LogService GET.
 *
 * @param asyncResp Async response object.
 */
inline void getJournalSyslogFilters(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp)
{
    dbus::utility::getProperty<std::string>(
        journalConfigService, journalConfigPath, journalConfigIface,
        "LowestSeverity",
        [asyncResp](const boost::system::error_code& ec,
                    const std::string& entryLevel) {
        if (ec)
        {
            // Journal.Config is meson-optional. Any Get failure must not
            // fail LogService GET.
            BMCWEB_LOG_DEBUG(
                "D-BUS error getting LowestSeverity; omitting SyslogFilters {}",
                ec);
            return;
        }
        auto severity = toRedfishSyslogSeverity(entryLevel);
        if (!severity)
        {
            BMCWEB_LOG_ERROR(
                "Unknown LowestSeverity {}; omitting SyslogFilters",
                entryLevel);
            return;
        }
        nlohmann::json::object_t filter;
        filter["LowestSeverity"] = *severity;
        filter["LowestSeverity@Redfish.AllowableValues"] =
            journalLowestSeverityAllowableValues();
        asyncResp->res.jsonValue["SyslogFilters"] =
            nlohmann::json::array_t{std::move(filter)};
    });
}

/**
 * @brief Parse PATCH SyslogFilters into a severity and whether to Set.
 *
 * false: error already in the response. true: payload is valid.
 * setProperty means the caller should Set severity. [] is All.
 * LowestSeverity is not required; omitted is valid and does not Set.
 *
 * @param syslogFilters PATCH SyslogFilters array.
 * @param asyncResp Async response object.
 * @param severity Parsed SyslogSeverity when setProperty is true.
 * @param setProperty True when a Set is required.
 */
inline bool parseJournalSyslogFilters(
    const std::vector<nlohmann::json::object_t>& syslogFilters,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    log_service::SyslogSeverity& severity, bool& setProperty)
{
    setProperty = false;
    if (syslogFilters.size() > 1)
    {
        BMCWEB_LOG_ERROR("SyslogFilters array size {} exceeds limit 1",
                         syslogFilters.size());
        messages::arraySizeTooLong(asyncResp->res, "SyslogFilters", 1);
        return false;
    }
    if (syslogFilters.empty())
    {
        severity = log_service::SyslogSeverity::All;
        setProperty = true;
        return true;
    }

    nlohmann::json::object_t filter = syslogFilters[0];
    std::optional<std::string> lowestSeverityStr;
    if (!json_util::readJsonObject(filter, asyncResp->res, "LowestSeverity",
                                   lowestSeverityStr))
    {
        return false;
    }
    if (!lowestSeverityStr)
    {
        return true;
    }

    std::optional<log_service::SyslogSeverity> lowestSeverity =
        parseRedfishSyslogSeverity(*lowestSeverityStr);
    if (!lowestSeverity)
    {
        BMCWEB_LOG_ERROR("Unknown SyslogFilters LowestSeverity {}",
                         *lowestSeverityStr);
        messages::propertyValueNotInList(asyncResp->res, *lowestSeverityStr,
                                         "SyslogFilters/LowestSeverity");
        return false;
    }
    severity = *lowestSeverity;
    setProperty = true;
    return true;
}

/**
 * @brief Set Journal.Config LowestSeverity from a Redfish SyslogSeverity.
 *
 * @param asyncResp Async response object.
 * @param severity Redfish SyslogSeverity to Set.
 */
inline void setJournalLowestSeverity(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    log_service::SyslogSeverity severity)
{
    auto entryLevel = toDbusEntryLevel(severity);
    if (!entryLevel)
    {
        BMCWEB_LOG_ERROR("Unmapped SyslogSeverity {}",
                         static_cast<int>(severity));
        messages::internalError(asyncResp->res);
        return;
    }

    setDbusProperty(asyncResp, "SyslogFilters/LowestSeverity",
                    journalConfigService,
                    sdbusplus::message::object_path(journalConfigPath),
                    journalConfigIface, "LowestSeverity", *entryLevel);
}

/**
 * @brief Handle PATCH on Managers/{ManagerId}/LogServices/Journal
 *
 * Parses SyslogFilters and Sets Journal.Config LowestSeverity when the
 * payload requires a write. Omitted SyslogFilters does not Set.
 *
 * @param app Crow application.
 * @param req Incoming HTTP request.
 * @param asyncResp Async response object.
 * @param managerId Manager identifier from the Redfish URI.
 */
inline void handleManagersLogServiceJournalPatch(
    App& app, const crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& managerId)
{
    if (!redfish::setUpRedfishRoute(app, req, asyncResp))
    {
        return;
    }

    if (managerId != BMCWEB_REDFISH_MANAGER_URI_NAME)
    {
        messages::resourceNotFound(asyncResp->res, "Manager", managerId);
        return;
    }

    std::optional<std::vector<nlohmann::json::object_t>> syslogFilters;
    if (!json_util::readJsonPatch(req, asyncResp->res, "SyslogFilters",
                                  syslogFilters))
    {
        BMCWEB_LOG_DEBUG("Journal LogService PATCH readJsonPatch error");
        return;
    }

    // SyslogFilters is not required on LogService. Omitted does not Set
    // (DSP0266 PATCH). A misspelled key is 400 PropertyUnknown above.
    if (syslogFilters)
    {
        log_service::SyslogSeverity severity{};
        bool setProperty = false;
        if (!parseJournalSyslogFilters(*syslogFilters, asyncResp, severity,
                                       setProperty))
        {
            return;
        }
        if (setProperty)
        {
            return setJournalLowestSeverity(asyncResp, severity);
        }
    }
    messages::success(asyncResp->res);
}

/**
 * @brief Register PATCH on Managers/{ManagerId}/LogServices/Journal.
 *
 * @param app Crow application.
 */
inline void requestRoutesJournalSyslogFilters(App& app)
{
    BMCWEB_ROUTE(app, "/redfish/v1/Managers/<str>/LogServices/Journal/")
        .privileges(redfish::privileges::patchLogService)
        .methods(boost::beast::http::verb::patch)(std::bind_front(
            handleManagersLogServiceJournalPatch, std::ref(app)));
}

} // namespace redfish
