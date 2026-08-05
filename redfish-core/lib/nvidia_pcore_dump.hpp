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

#include "bmcweb_config.h"

#include "app.hpp"
#include "dbus_utility.hpp"
#include "error_messages.hpp"
#include "logging.hpp"
#include "query.hpp"
#include "registries/privilege_registry.hpp"
#include "task.hpp"
#include "utils/dbus_utils.hpp"
#include "utils/json_utils.hpp"
#include "utils/nvidia_dump_utils.hpp"
#include "utils/processor_utils.hpp"

#include <boost/url/format.hpp>
#include <nlohmann/json.hpp>
#include <sdbusplus/unpack_properties.hpp>

#include <algorithm>
#include <array>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace redfish
{
namespace nvidia_pcore_dump
{

// Fully-qualified action name, used only in messages:: arguments.
constexpr const char* collectPCoreDumpActionName =
    "NvidiaProcessor.CollectPCoreDump";

// The interface pldmd publishes on the per-CPU PCore dump trigger, and the
// association edge that names the CPU it dumps. The effecter object carries
// ("cpu", "pcore_dump_control", <cpu inventory path>), so walking
// <cpu>/pcore_dump_control from the Processor side reaches exactly one
// trigger. Resolving by association rather than by decoding the device's
// auxiliary name keeps every PDR and terminus name out of bmcweb.
//
// Only MinPCoreId and MaxPCoreId are read here. The interface's CreateDump
// method is called by the dump collector, never by bmcweb, which reaches the
// collection through the dump manager instead.
constexpr const char* pcoreDumpInterface = "com.nvidia.PCoreDump";
constexpr const char* pcoreDumpAssociation = "pcore_dump_control";

// The Processor resource is scoped to Vera CPUs: an Accelerator never carries
// a PCore dump trigger, and gating on the interface here means the action is
// not advertised on one even if an association ever appeared.
constexpr const char* cpuInventoryInterface =
    "xyz.openbmc_project.Inventory.Item.Cpu";

// AdditionalData keys consumed by the dump manager's PCoreDump dispatch. The
// selector list is a Redfish-level concept flattened to a comma-separated
// string on this hop; no D-Bus interface carries the array.
constexpr const char* pcoreDiagnosticType = "PCoreDump";
constexpr const char* pcoreIdsKey = "PCoreIds";

/**
 * @brief Per-element selector bounds, mirrored from the trigger's PDR.
 */
struct PCoreIdRange
{
    uint64_t min = 0;
    uint64_t max = 0;
};

/**
 * @brief A resolved PCore dump trigger and the selector range it accepts.
 */
struct PCoreDumpTrigger
{
    std::string service;
    std::string path;
    PCoreIdRange bounds;
};

/**
 * @brief Outcome of resolving a CPU's PCore dump trigger.
 *
 * Absence and failure are deliberately distinct. A CPU whose firmware exposes
 * no trigger legitimately has none, and the action is simply not advertised.
 * A trigger that exists but cannot be read is a backend fault instead, and
 * reporting that as "no such Processor" would hide a live failure behind a
 * 404 that claims the CPU never supported the action.
 */
enum class TriggerLookup
{
    Found,
    Absent,
    Failed,
};

struct TriggerResult
{
    TriggerLookup status = TriggerLookup::Absent;
    PCoreDumpTrigger trigger;
};

using TriggerCallback = std::function<void(const TriggerResult&)>;

/**
 * @brief Sort and de-duplicate a requested selector list.
 *
 * Duplicates are collapsed rather than rejected, so PCoreIds [2,2,2] triggers
 * one collection.
 */
inline std::vector<uint64_t> normalizePCoreIds(std::vector<uint64_t> ids)
{
    std::ranges::sort(ids);
    const auto duplicates = std::ranges::unique(ids);
    ids.erase(duplicates.begin(), duplicates.end());
    return ids;
}

/**
 * @brief Render selectors for the PCoreIds AdditionalData value.
 *
 * An empty list renders as the empty string, which the dump manager reads as
 * "every PCore this trigger advertises". That is the same value an absent
 * PCoreIds produces, so omitting the member and passing [] behave alike.
 */
inline std::string formatPCoreIds(const std::vector<uint64_t>& ids)
{
    std::string out;
    for (const uint64_t id : ids)
    {
        if (!out.empty())
        {
            out += ',';
        }
        out += std::to_string(id);
    }
    return out;
}

/**
 * @brief Return the first selector outside the advertised range, if any.
 *
 * The whole request is rejected on the first bad element, before any D-Bus
 * call, so a mixed list like [1,7] creates no entry and no task.
 */
inline std::optional<uint64_t> firstOutOfRange(const std::vector<uint64_t>& ids,
                                               PCoreIdRange bounds)
{
    for (const uint64_t id : ids)
    {
        if (id < bounds.min || id > bounds.max)
        {
            return id;
        }
    }
    return std::nullopt;
}

/**
 * @brief Build the CreateDump a{sv} payload.
 *
 * Every value is a string: the dump manager parses the selector list itself,
 * and keeping the construction in one pure function makes it assertable from a
 * unit test.
 */
inline nvidia_dump_utils::DumpCreateParams buildPCoreCreateDumpParams(
    const std::string& processorId, const std::string& pcoreIds,
    const std::string& originatorId)
{
    nvidia_dump_utils::DumpCreateParams params;
    params.emplace_back("DiagnosticType", std::string(pcoreDiagnosticType));
    params.emplace_back("DeviceType", processorId);
    params.emplace_back(pcoreIdsKey, pcoreIds);

    if (!originatorId.empty())
    {
        params.emplace_back(
            "xyz.openbmc_project.Dump.Create.CreateParameters.OriginatorId",
            originatorId);
        params.emplace_back(
            "xyz.openbmc_project.Dump.Create.CreateParameters.OriginatorType",
            std::string(
                "xyz.openbmc_project.Common.OriginatedBy.OriginatorTypes.Client"));
    }
    return params;
}

/**
 * @brief Build the CollectPCoreDump ActionInfo body.
 *
 * PCoreIds is the only parameter: the CPU is named by the URI. MinimumValue
 * and MaximumValue are per-element bounds taken from the resolved trigger, so
 * they track the device's PDR rather than a compiled-in guess. The
 * "absent or empty means all PCores" contract lives in the CSDL
 * LongDescription, because an ActionInfo Parameter has no Description.
 *
 * Split out of the route handler so the payload is assertable from a unit test
 * without a crow::App or a live bus.
 */
inline void buildCollectPCoreDumpActionInfo(
    nlohmann::json& jsonValue, std::string_view systemId,
    std::string_view processorId, PCoreIdRange bounds)
{
    jsonValue["@odata.type"] = "#ActionInfo.v1_2_0.ActionInfo";
    jsonValue["@odata.id"] = boost::urls::format(
        "/redfish/v1/Systems/{}/Processors/{}/Oem/Nvidia/CollectPCoreDumpActionInfo",
        systemId, processorId);
    jsonValue["Id"] = "CollectPCoreDumpActionInfo";
    jsonValue["Name"] = "Collect PCore Dump Action Info";
    jsonValue["Description"] =
        "Parameters for collecting per-PCore dumps from this Vera CPU";

    nlohmann::json::array_t parameters;

    nlohmann::json::object_t pcoreIds;
    pcoreIds["Name"] = "PCoreIds";
    pcoreIds["Required"] = false;
    pcoreIds["DataType"] = "NumberArray";
    pcoreIds["MinimumValue"] = bounds.min;
    pcoreIds["MaximumValue"] = bounds.max;
    parameters.emplace_back(std::move(pcoreIds));

    jsonValue["Parameters"] = std::move(parameters);
}

/**
 * @brief Write the Actions/Oem advertisement onto a Processor resource.
 *
 * Processors carry no Actions member today, so this introduces one. Only
 * called once a trigger has resolved, so the action is advertised exactly on
 * the CPUs that can serve it.
 */
inline void buildPCoreDumpAdvertisement(nlohmann::json& jsonValue,
                                        std::string_view systemId,
                                        std::string_view processorId)
{
    nlohmann::json& action =
        jsonValue["Actions"]["Oem"]["#NvidiaProcessor.CollectPCoreDump"];
    action["target"] = boost::urls::format(
        "/redfish/v1/Systems/{}/Processors/{}/Actions/Oem/NvidiaProcessor.CollectPCoreDump",
        systemId, processorId);
    action["@Redfish.ActionInfo"] = boost::urls::format(
        "/redfish/v1/Systems/{}/Processors/{}/Oem/Nvidia/CollectPCoreDumpActionInfo",
        systemId, processorId);
}

/**
 * @brief Resolve a CPU inventory object to its PCore dump trigger.
 *
 * Walks the dedicated pcore_dump_control association rather than matching the
 * trigger's object name against the Processor index, so no terminus-to-CPU
 * correspondence is assumed anywhere in bmcweb.
 *
 * The callback receives Absent whenever the CPU has no trigger: no
 * association, no object behind it, or a trigger advertising an empty range.
 * It receives Failed only once a trigger is known to exist and implement the
 * interface, so a property read that fails is reported as a backend fault
 * rather than as a CPU that never supported the action.
 *
 * There is deliberately no fallback to a broader association such as
 * all_controls, because a chassis-level edge would match both CPUs' triggers
 * and could attribute one CPU's dump to the other.
 */
inline void resolvePCoreDumpTrigger(const std::string& cpuObjectPath,
                                    TriggerCallback&& callback)
{
    dbus::utility::getAssociationEndPoints(
        cpuObjectPath + "/" + pcoreDumpAssociation,
        [callback = std::move(callback)](
            const boost::system::error_code& ec,
            const dbus::utility::MapperEndPoints& endpoints) mutable {
            if (ec || endpoints.empty())
            {
                // The association is optional: a CPU whose firmware exposes
                // no trigger simply has no edge to walk, and the mapper
                // reports that as an error rather than an empty result.
                BMCWEB_LOG_DEBUG("No PCore dump trigger associated with CPU");
                callback({TriggerLookup::Absent, {}});
                return;
            }

            const std::string& triggerPath = endpoints.front();
            static constexpr std::array<std::string_view, 1> interfaces = {
                pcoreDumpInterface};

            // Discover the owning service rather than assuming pldmd's
            // well-known name, and confirm the interface while doing it.
            dbus::utility::getDbusObject(
                triggerPath, interfaces,
                [callback = std::move(callback), triggerPath](
                    const boost::system::error_code& ec2,
                    const dbus::utility::MapperGetObject& object) mutable {
                    if (ec2 || object.empty())
                    {
                        // Nothing behind the association implements the
                        // interface, so there is no trigger to read.
                        BMCWEB_LOG_ERROR(
                            "PCore dump trigger {} does not implement {}",
                            triggerPath, pcoreDumpInterface);
                        callback({TriggerLookup::Absent, {}});
                        return;
                    }

                    const std::string service = object.front().first;
                    dbus::utility::getAllProperties(
                        service, triggerPath, pcoreDumpInterface,
                        [callback = std::move(callback), service,
                         triggerPath](const boost::system::error_code& ec3,
                                      const dbus::utility::DBusPropertiesMap&
                                          properties) mutable {
                            if (ec3)
                            {
                                // The trigger exists and implements the
                                // interface, so a read that fails here is a
                                // backend fault, not a CPU without support.
                                BMCWEB_LOG_ERROR(
                                    "Failed reading PCore dump bounds from {}",
                                    triggerPath);
                                callback({TriggerLookup::Failed, {}});
                                return;
                            }

                            uint64_t minPCoreId = 0;
                            uint64_t maxPCoreId = 0;
                            if (!sdbusplus::unpackPropertiesNoThrow(
                                    dbus_utils::UnpackErrorPrinter(),
                                    properties, "MinPCoreId", minPCoreId,
                                    "MaxPCoreId", maxPCoreId))
                            {
                                // Same reasoning: the trigger is there, but
                                // it is not carrying the bounds it must.
                                callback({TriggerLookup::Failed, {}});
                                return;
                            }

                            // An inverted range means the device advertised
                            // nothing usable; treat it as no trigger rather
                            // than publishing bounds no value can satisfy.
                            if (minPCoreId > maxPCoreId)
                            {
                                BMCWEB_LOG_ERROR(
                                    "PCore dump trigger {} advertises an empty selector range {}..{}",
                                    triggerPath, minPCoreId, maxPCoreId);
                                callback({TriggerLookup::Absent, {}});
                                return;
                            }

                            callback(
                                {TriggerLookup::Found,
                                 PCoreDumpTrigger{
                                     service, triggerPath,
                                     PCoreIdRange{minPCoreId, maxPCoreId}}});
                        });
                });
        });
}

/**
 * @brief Advertise the action on a Processor that can serve it.
 *
 * Called from the Processor GET population pass. Accelerators are rejected on
 * deviceType before any D-Bus traffic; a CPU without a trigger simply gains no
 * Actions member.
 */
inline void advertisePCoreDump(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& processorId, const std::string& objectPath,
    const std::string& deviceType)
{
    if (deviceType != cpuInventoryInterface)
    {
        return;
    }

    resolvePCoreDumpTrigger(
        objectPath, [asyncResp, processorId](const TriggerResult& result) {
            if (result.status == TriggerLookup::Failed)
            {
                messages::internalError(asyncResp->res);
                return;
            }
            if (result.status != TriggerLookup::Found)
            {
                return;
            }
            buildPCoreDumpAdvertisement(asyncResp->res.jsonValue,
                                        BMCWEB_REDFISH_SYSTEM_URI_NAME,
                                        processorId);
        });
}

inline void handleCollectPCoreDumpActionInfoGet(
    crow::App& app, const crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& systemName, const std::string& processorId)
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

    redfish::processor_utils::getProcessorObject(
        asyncResp, processorId,
        [resolved = std::make_shared<bool>(false)](
            const std::shared_ptr<bmcweb::AsyncResp>& asyncResp1,
            const std::string& processorId1, const std::string& objectPath,
            const dbus::utility::MapperServiceMap& /*serviceMap*/,
            const std::string& deviceType) {
            if (deviceType != cpuInventoryInterface)
            {
                messages::resourceNotFound(asyncResp1->res, "Processor",
                                           processorId1);
                return;
            }

            // getProcessorObject deliberately does not stop at the first
            // match -- a CPU's inventory data can be split across services --
            // so the handler runs once per matching object. Resolve the
            // trigger for the first CPU-typed match only, rather than firing
            // a second round of D-Bus lookups that would rewrite the same
            // body. The flag is shared rather than captured mutable so it
            // survives any copy of this handler.
            if (*resolved)
            {
                return;
            }
            *resolved = true;

            resolvePCoreDumpTrigger(
                objectPath,
                [asyncResp1, processorId1](const TriggerResult& result) {
                    if (result.status == TriggerLookup::Failed)
                    {
                        messages::internalError(asyncResp1->res);
                        return;
                    }
                    if (result.status != TriggerLookup::Found)
                    {
                        messages::resourceNotFound(asyncResp1->res, "Processor",
                                                   processorId1);
                        return;
                    }
                    buildCollectPCoreDumpActionInfo(
                        asyncResp1->res.jsonValue,
                        BMCWEB_REDFISH_SYSTEM_URI_NAME, processorId1,
                        result.trigger.bounds);
                });
        });
}

inline void handleCollectPCoreDumpPost(
    crow::App& app, const crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& systemName, const std::string& processorId)
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

    // The only member is optional: an empty body collects every PCore of the
    // CPU named in the URI. The elements must stay unsigned -- a signed vector
    // would still compile but would bypass readJsonAction's negative-value
    // rejection, so PCoreIds [-1] would reach the range check as a huge
    // positive.
    std::optional<std::vector<uint64_t>> requestedIds;
    if (!json_util::readJsonAction(req, asyncResp->res, "PCoreIds",
                                   requestedIds))
    {
        return;
    }

    std::vector<uint64_t> pcoreIds =
        normalizePCoreIds(requestedIds.value_or(std::vector<uint64_t>{}));

    std::string originatorId;
    if (req.session != nullptr)
    {
        originatorId = req.session->clientIp;
    }

    redfish::processor_utils::getProcessorObject(
        asyncResp, processorId,
        [dispatched = std::make_shared<bool>(false),
         payload = std::make_shared<task::Payload>(req),
         pcoreIds = std::move(pcoreIds),
         originatorId = std::move(originatorId)](
            const std::shared_ptr<bmcweb::AsyncResp>& asyncResp1,
            const std::string& processorId1, const std::string& objectPath,
            const dbus::utility::MapperServiceMap& /*serviceMap*/,
            const std::string& deviceType) {
            if (deviceType != cpuInventoryInterface)
            {
                messages::resourceNotFound(asyncResp1->res, "Processor",
                                           processorId1);
                return;
            }

            // getProcessorObject deliberately does not stop at the first
            // match -- a CPU's inventory data can be split across services --
            // so the handler runs once per matching object. Dispatching twice
            // would create two dump entries for one request and move from an
            // already-moved payload. The flag and the payload are shared
            // rather than captured mutable so they survive any copy of this
            // handler.
            if (*dispatched)
            {
                return;
            }
            *dispatched = true;

            // pcoreIds and originatorId are captured by copy rather than
            // moved: this lambda is not mutable, so the enclosing captures are
            // const and a move would silently degrade to a copy anyway. Both
            // are tiny -- at most six selectors and a client IP.
            resolvePCoreDumpTrigger(
                objectPath, [asyncResp1, processorId1, payload, pcoreIds,
                             originatorId](const TriggerResult& result) {
                    if (result.status == TriggerLookup::Failed)
                    {
                        messages::internalError(asyncResp1->res);
                        return;
                    }
                    if (result.status != TriggerLookup::Found)
                    {
                        messages::resourceNotFound(asyncResp1->res, "Processor",
                                                   processorId1);
                        return;
                    }

                    // Bounds come from the device, so the whole request is
                    // rejected before any dump entry exists.
                    const std::optional<uint64_t> badId =
                        firstOutOfRange(pcoreIds, result.trigger.bounds);
                    if (badId)
                    {
                        messages::actionParameterValueOutOfRange(
                            asyncResp1->res, std::to_string(*badId), "PCoreIds",
                            collectPCoreDumpActionName);
                        return;
                    }

                    nvidia_dump_utils::createSystemDump(
                        asyncResp1, std::move(*payload),
                        buildPCoreCreateDumpParams(processorId1,
                                                   formatPCoreIds(pcoreIds),
                                                   originatorId),
                        collectPCoreDumpActionName, pcoreIdsKey);
                });
        });
}

} // namespace nvidia_pcore_dump

inline void requestRoutesCollectPCoreDump(App& app)
{
    BMCWEB_ROUTE(
        app,
        "/redfish/v1/Systems/<str>/Processors/<str>/Actions/Oem/NvidiaProcessor.CollectPCoreDump/")
        .privileges(redfish::privileges::postProcessor)
        .methods(boost::beast::http::verb::post)(std::bind_front(
            nvidia_pcore_dump::handleCollectPCoreDumpPost, std::ref(app)));

    BMCWEB_ROUTE(
        app,
        "/redfish/v1/Systems/<str>/Processors/<str>/Oem/Nvidia/CollectPCoreDumpActionInfo/")
        .privileges(redfish::privileges::getActionInfo)
        .methods(boost::beast::http::verb::get)(std::bind_front(
            nvidia_pcore_dump::handleCollectPCoreDumpActionInfoGet,
            std::ref(app)));
}

} // namespace redfish
