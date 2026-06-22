/*
 * SPDX-FileCopyrightText: Copyright (c) 2025 NVIDIA CORPORATION &
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
#include "async_resp.hpp"
#include "dbus_utility.hpp"
#include "error_messages.hpp"
#include "query.hpp"
#include "registries/privilege_registry.hpp"
#include "utils/json_utils.hpp"
#include "utils/nvidia_async_set_utils.hpp"

#include <boost/url/format.hpp>
#include <sdbusplus/message/native_types.hpp>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <tuple>
#include <variant>
#include <vector>

namespace redfish
{
namespace nvidia_aggregate_power
{

// Single source of truth for the async-set timeout in seconds, shared with the
// stock single-GPU setters. Both the chrono value passed to the bulk setter and
// the operator-facing "retry after N seconds" message text derive from this.
inline constexpr unsigned int setProcessorPowerLimitsTimeoutSeconds =
    nvidia_async_operation_utils::asyncSetTimeoutSeconds;

// "... retry after <N> seconds" — derived from the timeout constant so the
// message text and the chrono timeout never drift.
inline std::string commandBusyRetryMessage()
{
    return "Command busy, retry after " +
           std::to_string(setProcessorPowerLimitsTimeoutSeconds) + " seconds";
}

// "Async result timeout after <N> seconds" — derived from the timeout constant.
inline std::string asyncResultTimeoutMessage()
{
    return "Async result timeout after " +
           std::to_string(setProcessorPowerLimitsTimeoutSeconds) + " seconds";
}

// Power-limit dimension. Total maps to the GPU accelerator object itself; Base
// maps to that object's Base_Power_Limit association endpoint.
enum class PowerLimitDimension
{
    Total,
    Base
};

// One parsed element of the request "ProcessorLimits" array.
struct ProcessorLimitEntry
{
    std::string processorId;
    std::optional<uint32_t> totalPowerLimitWatts;
    std::optional<uint32_t> basePowerLimitWatts;
};

// Per-(GPU, dimension) write result accumulated during fan-out.
struct SetOutcome
{
    std::string processorId;
    PowerLimitDimension dimension{PowerLimitDimension::Total};
    std::string status;  // OK / OperationFailed / ... (see mapAsyncStatus)
    std::string message; // human readable detail (empty on OK)
};

// Resolved D-Bus targets for one GPU, from the single accelerator subtree.
struct GpuPaths
{
    std::string processorId;
    std::string gpuObjectPath;                     // Total target
    std::optional<std::string> basePowerLimitPath; // Base assoc endpoint
    // NB: the Cap-set destination service is resolved PER objectPath at
    // dispatch time via GetObject (ResolveServiceAndDispatch) — the accelerator
    // object's owner is intentionally NOT reused as the destination (correction
    // delta).
};

// Heap-allocated (make_shared) request-scoped accumulator.
// pendingCount is initialized to attemptCount + 1 (sentinel) and is
// thereafter only decremented; assembleResponse is called by whichever
// decrement observes the 1->0 transition.
struct BulkSetContext
{
    std::shared_ptr<bmcweb::AsyncResp> asyncResp;
    std::vector<SetOutcome> results;
    std::atomic<size_t> pendingCount{0};
    bool responseSent{false};
};

inline std::string_view dimensionFieldName(PowerLimitDimension dimension)
{
    return dimension == PowerLimitDimension::Total
               ? "PowerLimitWatts"
               : "GPUBasePowerWatts";
}

// Translates a com.nvidia.Async.Set status string into a SetOutcome status +
// message per the async-status mapping table.
inline SetOutcome mapAsyncStatus(const std::string& processorId,
                                 PowerLimitDimension dimension, uint32_t watts,
                                 const std::string& status)
{
    namespace async_utils = nvidia_async_operation_utils;

    SetOutcome outcome;
    outcome.processorId = processorId;
    outcome.dimension = dimension;

    if (status == async_utils::asyncStatusValueSuccess)
    {
        outcome.status = "OK";
        return outcome;
    }

    if (status == async_utils::asyncStatusValueWriteFailure)
    {
        outcome.status = "OperationFailed";
        outcome.message = "NSM write-path failure";
        return outcome;
    }

    if (status == async_utils::asyncStatusValueUnavailable)
    {
        outcome.status = "Unavailable";
        outcome.message = commandBusyRetryMessage();
        return outcome;
    }

    if (status == async_utils::asyncStatusValueTimeout)
    {
        outcome.status = "OperationTimeout";
        outcome.message = asyncResultTimeoutMessage();
        return outcome;
    }

    if (status == async_utils::asyncStatusValueInvalidArgument)
    {
        outcome.status = "InvalidArgument";
        outcome.message = std::string(dimensionFieldName(dimension)) + " " +
                          std::to_string(watts) + " is outside allowable range";
        return outcome;
    }

    // Catch-all: any other / interface-not-found / internal failure.
    outcome.status = "InternalError";
    outcome.message = "Unexpected internal error";
    return outcome;
}

// Builds the final HTTP response from ctx->results. 204 if every dispatched set
// is OK, else 500 + DMTF Base.1.12.0.OperationFailed + error.Oem.Nvidia.
// FailedGPUs. Sole writer of asyncResp->res;
// idempotent-safe via responseSent.
inline void assembleResponse(const std::shared_ptr<BulkSetContext>& ctx)
{
    if (ctx->responseSent)
    {
        BMCWEB_LOG_ERROR(
            "SetProcessorPowerLimits: assembleResponse invoked twice");
        return;
    }
    ctx->responseSent = true;

    bool anyFailure = false;
    for (const SetOutcome& outcome : ctx->results)
    {
        if (outcome.status != "OK")
        {
            anyFailure = true;
            break;
        }
    }

    if (!anyFailure)
    {
        ctx->asyncResp->res.result(boost::beast::http::status::no_content);
        return;
    }

    // Build FailedGPUs dict: one key per ProcessorId, each carrying a
    // per-failed- dimension {Status, Message} sub-object keyed by the dimension
    // field name (PowerLimitWatts / GPUBasePowerWatts). A GPU that fails on
    // both dimensions reports both; a dimension that succeeded is omitted.
    nlohmann::json failedGpus = nlohmann::json::object();
    for (const SetOutcome& outcome : ctx->results)
    {
        if (outcome.status == "OK")
        {
            continue;
        }
        std::string dimField(dimensionFieldName(outcome.dimension));
        nlohmann::json entry = nlohmann::json::object();
        entry["Status"] = outcome.status;
        entry["Message"] = outcome.message;
        failedGpus[outcome.processorId][dimField] = std::move(entry);
    }

    // messages::operationFailed adds the DMTF Base OperationFailed
    // ExtendedInfo but sets HTTP 502; set 500 afterward to honor the
    // documented response contract.
    messages::operationFailed(ctx->asyncResp->res);
    ctx->asyncResp->res.result(
        boost::beast::http::status::internal_server_error);
    ctx->asyncResp->res.jsonValue["error"]["Oem"]["Nvidia"]["FailedGPUs"] =
        std::move(failedGpus);
}

// Records one terminal per-(GPU,dimension) outcome and performs the single
// atomic decrement-and-test; the caller that observes 1->0 calls
// assembleResponse.
inline void recordOutcome(const std::shared_ptr<BulkSetContext>& ctx,
                          const SetOutcome& outcome)
{
    ctx->results.push_back(outcome);

    if (ctx->pendingCount.fetch_sub(1) == 1)
    {
        assembleResponse(ctx);
    }
}

// Custom aggregating callback (replaces the stock PatchPowerCapCallback). It
// records the outcome into ctx->results and never writes asyncResp->res.
// Named functor holding a shared_ptr<BulkSetContext>;
// callbacks never throw (mapping is wrapped in try/catch).
class BulkSetCallback
{
  public:
    BulkSetCallback(std::shared_ptr<BulkSetContext> ctxIn,
                    std::string processorIdIn, PowerLimitDimension dimensionIn,
                    uint32_t wattsIn) :
        ctx(std::move(ctxIn)), processorId(std::move(processorIdIn)),
        dimension(dimensionIn), watts(wattsIn)
    {}

    void operator()(const std::string& status) const
    {
        try
        {
            recordOutcome(
                ctx, mapAsyncStatus(processorId, dimension, watts, status));
        }
        catch (...)
        {
            BMCWEB_LOG_ERROR(
                "SetProcessorPowerLimits: exception in BulkSetCallback for {}",
                processorId);
            SetOutcome outcome;
            outcome.processorId = processorId;
            outcome.dimension = dimension;
            outcome.status = "InternalError";
            outcome.message = "Unexpected internal error";
            recordOutcome(ctx, outcome);
        }
    }

  private:
    std::shared_ptr<BulkSetContext> ctx;
    std::string processorId;
    PowerLimitDimension dimension;
    uint32_t watts;
};

// Resolves the D-Bus service that hosts a specific objectPath via the canonical
// bmcweb GetObject pattern, then issues the async PowerCap set on the resolved
// owner. Named functor (no inline lambda) holding the bulk
// context plus the dimension's target. This is the GetObject completion of the
// two-step dispatch chain introduced to stop reusing the accelerator object's
// owner as the Cap-set destination (correction delta).
//
// pendingCount invariant: this dimension already counted toward
// attemptCount + 1 up-front; pendingCount is NOT incremented here. Both
// terminal branches record EXACTLY ONE SetOutcome:
//   - GetObject error, or no service hosting Control.Power.Cap → recordOutcome
//     once (Unavailable / InternalError) and no dispatch.
//   - resolved owner found → dispatch the async set; its BulkSetCallback fires
//     recordOutcome exactly once.
// Never zero (count leak), never two (double decrement).
class ResolveServiceAndDispatch
{
  public:
    ResolveServiceAndDispatch(
        std::shared_ptr<BulkSetContext> ctxIn, std::string processorIdIn,
        std::string objectPathIn, PowerLimitDimension dimensionIn,
        uint32_t wattsIn) :
        ctx(std::move(ctxIn)), processorId(std::move(processorIdIn)),
        objectPath(std::move(objectPathIn)), dimension(dimensionIn),
        watts(wattsIn)
    {}

    void operator()(const boost::system::error_code& ec,
                    const dbus::utility::MapperGetObject& object) const
    {
        if (ec)
        {
            // GetObject (ObjectMapper) failed for this objectPath — the target
            // is unreachable / unknown. Record one terminal outcome so
            // pendingCount stays balanced; no dispatch.
            BMCWEB_LOG_ERROR(
                "SetProcessorPowerLimits: GetObject failed for {} ({}): {}",
                objectPath, processorId, ec.message());
            recordResolveFailure("Unavailable", commandBusyRetryMessage());
            return;
        }

        // Find the service that actually hosts the Power.Cap interface on this
        // objectPath, mirroring patchPowerLimit / patchBasePowerWattsByService.
        for (const auto& [serv, interfaces] : object)
        {
            if (std::ranges::find(interfaces,
                                  "xyz.openbmc_project.Control.Power.Cap") ==
                interfaces.end())
            {
                continue;
            }

            // PowerCap D-Bus type is (bu) = {persistency, watts}; persistency
            // is false (volatile / oneshot).
            std::tuple<bool, uint32_t> reqPowerLimit(false, watts);

            nvidia_async_operation_utils::doGenericSetAsyncForBulk(
                ctx->asyncResp,
                std::chrono::seconds(setProcessorPowerLimitsTimeoutSeconds),
                serv, objectPath, "xyz.openbmc_project.Control.Power.Cap",
                "PowerCap",
                std::variant<std::tuple<bool, uint32_t>>(reqPowerLimit),
                BulkSetCallback{ctx, processorId, dimension, watts});
            return;
        }

        // No service on this objectPath hosts Control.Power.Cap — record one
        // terminal InternalError; no dispatch.
        BMCWEB_LOG_ERROR(
            "SetProcessorPowerLimits: no Power.Cap owner for {} ({})",
            objectPath, processorId);
        recordResolveFailure("InternalError", "Unexpected internal error");
    }

  private:
    void recordResolveFailure(const std::string& status,
                              const std::string& message) const
    {
        SetOutcome outcome;
        outcome.processorId = processorId;
        outcome.dimension = dimension;
        outcome.status = status;
        outcome.message = message;
        recordOutcome(ctx, outcome);
    }

    std::shared_ptr<BulkSetContext> ctx;
    std::string processorId;
    std::string objectPath;
    PowerLimitDimension dimension;
    uint32_t watts;
};

// Starts one per-dimension dispatch. Resolves the hosting service for the
// dimension's object path via GetObject (filtered by com.nvidia.Async.Set),
// then dispatches the async PowerCap set to the resolved owner. Does NOT reuse
// the accelerator object's owner (correction delta) and does NOT mutate
// pendingCount (fixed up-front).
inline void dispatchOneSet(const std::shared_ptr<BulkSetContext>& ctx,
                           const GpuPaths& gpuPaths,
                           PowerLimitDimension dimension, uint32_t watts)
{
    const std::string& objectPath = dimension == PowerLimitDimension::Total
                                        ? gpuPaths.gpuObjectPath
                                        : *gpuPaths.basePowerLimitPath;

    dbus::utility::getDbusObject(
        objectPath,
        std::array<std::string_view, 1>{
            nvidia_async_operation_utils::setAsyncInterfaceName},
        ResolveServiceAndDispatch{ctx, gpuPaths.processorId, objectPath,
                                  dimension, watts});
}

// Final fan-out driver. By the time this runs every ProcessorId is validated;
// pendingCount has already been initialized to attemptCount + 1. Dispatches or
// immediately records every supplied dimension, then releases the sentinel with
// one final recordOutcome decrement.
class FanOutDriver
{
  public:
    FanOutDriver(std::shared_ptr<BulkSetContext> ctxIn,
                 std::vector<ProcessorLimitEntry> entriesIn,
                 std::vector<GpuPaths> gpuPathsIn) :
        ctx(std::move(ctxIn)), entries(std::move(entriesIn)),
        gpuPaths(std::move(gpuPathsIn))
    {}

    void operator()() const
    {
        for (const ProcessorLimitEntry& entry : entries)
        {
            const GpuPaths* paths = findPaths(entry.processorId);
            if (paths == nullptr)
            {
                // Should not happen — existence was validated pre-write. Record
                // both supplied dimensions as InternalError so pendingCount
                // stays balanced.
                recordMissing(entry);
                continue;
            }

            if (entry.totalPowerLimitWatts.has_value())
            {
                dispatchOneSet(ctx, *paths, PowerLimitDimension::Total,
                               *entry.totalPowerLimitWatts);
            }

            if (entry.basePowerLimitWatts.has_value())
            {
                if (paths->basePowerLimitPath.has_value())
                {
                    dispatchOneSet(ctx, *paths, PowerLimitDimension::Base,
                                   *entry.basePowerLimitWatts);
                }
                else
                {
                    // Base requested but the Base_Power_Limit association is
                    // empty (platform inconsistency) — record InternalError for
                    // the Base dimension; no dispatch.
                    SetOutcome outcome;
                    outcome.processorId = entry.processorId;
                    outcome.dimension = PowerLimitDimension::Base;
                    outcome.status = "InternalError";
                    outcome.message = "Unexpected internal error";
                    recordOutcome(ctx, outcome);
                }
            }
        }

        // Release the sentinel after every dimension has been dispatched or
        // recorded. This decrement is the one that can drive pendingCount to 0
        // when all dispatches completed synchronously.
        SetOutcome sentinel;
        sentinel.status = "OK"; // sentinel never affects the failure scan
        sentinel.processorId.clear();
        recordOutcome(ctx, sentinel);
    }

  private:
    const GpuPaths* findPaths(const std::string& processorId) const
    {
        for (const GpuPaths& paths : gpuPaths)
        {
            if (paths.processorId == processorId)
            {
                return &paths;
            }
        }
        return nullptr;
    }

    void recordMissing(const ProcessorLimitEntry& entry) const
    {
        if (entry.totalPowerLimitWatts.has_value())
        {
            SetOutcome outcome;
            outcome.processorId = entry.processorId;
            outcome.dimension = PowerLimitDimension::Total;
            outcome.status = "InternalError";
            outcome.message = "Unexpected internal error";
            recordOutcome(ctx, outcome);
        }
        if (entry.basePowerLimitWatts.has_value())
        {
            SetOutcome outcome;
            outcome.processorId = entry.processorId;
            outcome.dimension = PowerLimitDimension::Base;
            outcome.status = "InternalError";
            outcome.message = "Unexpected internal error";
            recordOutcome(ctx, outcome);
        }
    }

    std::shared_ptr<BulkSetContext> ctx;
    std::vector<ProcessorLimitEntry> entries;
    std::vector<GpuPaths> gpuPaths;
};

// Builds the request-scoped BulkSetContext with the sentinel-guarded
// pendingCount (attemptCount + 1) and drives the fan-out.
// Shared by both fan-out entry points: the no-Base-lookup path in
// DiscoverGpuPaths and the post-association-resolution path in
// ResolveBaseAndFanOut.
inline void launchFanOut(const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                         const std::vector<ProcessorLimitEntry>& entries,
                         const std::vector<GpuPaths>& gpuPaths)
{
    auto ctx = std::make_shared<BulkSetContext>();
    ctx->asyncResp = asyncResp;

    size_t attemptCount = 0;
    for (const ProcessorLimitEntry& entry : entries)
    {
        if (entry.totalPowerLimitWatts.has_value())
        {
            ++attemptCount;
        }
        if (entry.basePowerLimitWatts.has_value())
        {
            ++attemptCount;
        }
    }

    // Sentinel guard: +1 held by the fan-out driver.
    ctx->pendingCount.store(attemptCount + 1);

    FanOutDriver driver{ctx, entries, gpuPaths};
    driver();
}

// Resolves the Base_Power_Limit association endpoint for each GPU, then builds
// the BulkSetContext and starts the fan-out. Named functor invoked once per GPU
// association lookup completion; uses an outstanding-lookup counter so the
// fan-out starts only after every Base association has been resolved (or
// recorded as missing). No nested lambdas.
class ResolveBaseAndFanOut
{
  public:
    ResolveBaseAndFanOut(std::shared_ptr<bmcweb::AsyncResp> asyncRespIn,
                         std::vector<ProcessorLimitEntry> entriesIn,
                         std::shared_ptr<std::vector<GpuPaths>> gpuPathsIn,
                         std::shared_ptr<std::atomic<size_t>> outstandingIn,
                         std::string processorIdIn) :
        asyncResp(std::move(asyncRespIn)), entries(std::move(entriesIn)),
        gpuPaths(std::move(gpuPathsIn)), outstanding(std::move(outstandingIn)),
        processorId(std::move(processorIdIn))
    {}

    void operator()(const boost::system::error_code& ec,
                    const dbus::utility::MapperEndPoints& endpoints) const
    {
        for (GpuPaths& paths : *gpuPaths)
        {
            if (paths.processorId != processorId)
            {
                continue;
            }
            if (!ec && !endpoints.empty())
            {
                paths.basePowerLimitPath = endpoints.front();
            }
            // On error / empty: basePowerLimitPath stays nullopt; the fan-out
            // records InternalError for the Base dimension.
            break;
        }

        if (outstanding->fetch_sub(1) == 1)
        {
            launchFanOut(asyncResp, entries, *gpuPaths);
        }
    }

  private:
    std::shared_ptr<bmcweb::AsyncResp> asyncResp;
    std::vector<ProcessorLimitEntry> entries;
    std::shared_ptr<std::vector<GpuPaths>> gpuPaths;
    std::shared_ptr<std::atomic<size_t>> outstanding;
    std::string processorId;
};

// Emits the whole-request 400 for an out-of-set ProcessorId, matching the
// documented contract (Base.1.0.ActionParameterValueError,
// "ID <id> is not supported for setting Power Limits.").
inline void rejectUnsupportedProcessor(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp, const std::string& id)
{
    asyncResp->res.result(boost::beast::http::status::bad_request);
    nlohmann::json& error = asyncResp->res.jsonValue["error"];
    error["code"] = "Base.1.0.ActionParameterValueError";
    error["message"] =
        "ID " + id + " is not supported for setting Power Limits.";
    error["@Message.ExtendedInfo"] = nlohmann::json::array();
}

// Single accelerator GetSubTree: discovers the GPU set, validates every
// requested ProcessorId is in that set (whole-request 400 otherwise), resolves
// Total targets, and kicks off Base association resolution.
class DiscoverGpuPaths
{
  public:
    DiscoverGpuPaths(std::shared_ptr<bmcweb::AsyncResp> asyncRespIn,
                     std::vector<ProcessorLimitEntry> entriesIn) :
        asyncResp(std::move(asyncRespIn)), entries(std::move(entriesIn))
    {}

    void operator()(
        const boost::system::error_code& ec,
        const dbus::utility::MapperGetSubTreeResponse& subtree) const
    {
        if (ec)
        {
            BMCWEB_LOG_ERROR(
                "SetProcessorPowerLimits: accelerator GetSubTree failed: {}",
                ec.message());
            messages::internalError(asyncResp->res);
            return;
        }

        // validGpuSet + per-id Total target resolution from the single subtree
        // result. The Cap-set destination service is resolved later, per
        // objectPath, at dispatch time (correction delta).
        auto gpuPaths = std::make_shared<std::vector<GpuPaths>>();
        std::set<std::string> validGpuSet;
        for (const auto& [path, serviceMap] : subtree)
        {
            sdbusplus::message::object_path objectPath(path);
            std::string filename = objectPath.filename();
            if (filename.empty() || serviceMap.empty())
            {
                continue;
            }
            validGpuSet.insert(filename);

            GpuPaths paths;
            paths.processorId = filename;
            paths.gpuObjectPath = path;
            gpuPaths->push_back(std::move(paths));
        }

        // Existence validation (pre-write, whole-request): every requested
        // ProcessorId must be a GPU in the set.
        for (const ProcessorLimitEntry& entry : entries)
        {
            if (!validGpuSet.contains(entry.processorId))
            {
                rejectUnsupportedProcessor(asyncResp, entry.processorId);
                return;
            }
        }

        // Resolve the Base_Power_Limit association endpoint per requested GPU
        // that supplied a GPUBasePowerWatts. If none requested Base, start
        // the fan-out immediately.
        std::set<std::string> baseLookups;
        for (const ProcessorLimitEntry& entry : entries)
        {
            if (entry.basePowerLimitWatts.has_value())
            {
                baseLookups.insert(entry.processorId);
            }
        }

        if (baseLookups.empty())
        {
            // No association lookups needed — drive the fan-out directly.
            launchFanOut(asyncResp, entries, *gpuPaths);
            return;
        }

        auto outstanding =
            std::make_shared<std::atomic<size_t>>(baseLookups.size());
        for (const std::string& id : baseLookups)
        {
            const std::string* gpuObjectPath = nullptr;
            for (const GpuPaths& paths : *gpuPaths)
            {
                if (paths.processorId == id)
                {
                    gpuObjectPath = &paths.gpuObjectPath;
                    break;
                }
            }
            if (gpuObjectPath == nullptr)
            {
                // Already validated above; defensive — count down so the
                // fan-out still starts.
                if (outstanding->fetch_sub(1) == 1)
                {
                    launchFanOut(asyncResp, entries, *gpuPaths);
                }
                continue;
            }

            dbus::utility::getAssociationEndPoints(
                *gpuObjectPath + "/Base_Power_Limit",
                ResolveBaseAndFanOut{asyncResp, entries, gpuPaths, outstanding,
                                     id});
        }
    }

  private:
    std::shared_ptr<bmcweb::AsyncResp> asyncResp;
    std::vector<ProcessorLimitEntry> entries;
};

// Parses one raw "ProcessorLimits" element into a ProcessorLimitEntry, applying
// the structural validation rules. Returns false (and
// writes a 400) on any structural failure.
inline bool parseEntry(const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                       nlohmann::json& rawEntry, ProcessorLimitEntry& parsed)
{
    std::string processorId;
    std::optional<uint32_t> totalPowerLimitWatts;
    std::optional<uint32_t> basePowerLimitWatts;

    if (!json_util::readJson(rawEntry, asyncResp->res, "ProcessorId",
                             processorId, "PowerLimitWatts",
                             totalPowerLimitWatts, "GPUBasePowerWatts",
                             basePowerLimitWatts))
    {
        return false;
    }

    if (!totalPowerLimitWatts.has_value() && !basePowerLimitWatts.has_value())
    {
        messages::actionParameterMissing(
            asyncResp->res, "SetProcessorPowerLimits",
            "PowerLimitWatts or GPUBasePowerWatts");
        return false;
    }

    parsed.processorId = std::move(processorId);
    parsed.totalPowerLimitWatts = totalPowerLimitWatts;
    parsed.basePowerLimitWatts = basePowerLimitWatts;
    return true;
}

inline void handleSetProcessorPowerLimitsPost(
    crow::App& app, const crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& systemId)
{
    if (!redfish::setUpRedfishRoute(app, req, asyncResp))
    {
        return;
    }

    if (systemId != BMCWEB_REDFISH_SYSTEM_URI_NAME)
    {
        messages::resourceNotFound(asyncResp->res, "ComputerSystem", systemId);
        return;
    }

    std::vector<nlohmann::json> processorLimits;
    if (!json_util::readJsonAction(req, asyncResp->res, "ProcessorLimits",
                                   processorLimits))
    {
        return;
    }

    if (processorLimits.empty())
    {
        messages::actionParameterValueError(asyncResp->res, "ProcessorLimits",
                                            "SetProcessorPowerLimits");
        return;
    }

    std::vector<ProcessorLimitEntry> entries;
    entries.reserve(processorLimits.size());
    std::set<std::string> seenIds;
    for (nlohmann::json& rawEntry : processorLimits)
    {
        ProcessorLimitEntry parsed;
        if (!parseEntry(asyncResp, rawEntry, parsed))
        {
            return;
        }

        // Duplicate ProcessorId is a request-shape reject.
        if (!seenIds.insert(parsed.processorId).second)
        {
            messages::actionParameterValueError(asyncResp->res, "ProcessorId",
                                                "SetProcessorPowerLimits");
            return;
        }

        entries.push_back(std::move(parsed));
    }

    // Single accelerator subtree → discovery + ProcessorId validation + path
    // resolution → fan-out.
    constexpr std::array<std::string_view, 1> acceleratorInterfaces = {
        "xyz.openbmc_project.Inventory.Item.Accelerator"};
    dbus::utility::getSubTree("/xyz/openbmc_project/inventory", 0,
                              acceleratorInterfaces,
                              DiscoverGpuPaths{asyncResp, std::move(entries)});
}

inline void handleSetProcessorPowerLimitsActionInfoGet(
    crow::App& app, const crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& systemId)
{
    if (!redfish::setUpRedfishRoute(app, req, asyncResp))
    {
        return;
    }

    if (systemId != BMCWEB_REDFISH_SYSTEM_URI_NAME)
    {
        messages::resourceNotFound(asyncResp->res, "ComputerSystem", systemId);
        return;
    }

    nlohmann::json& jsonValue = asyncResp->res.jsonValue;
    jsonValue["@odata.type"] = "#ActionInfo.v1_2_0.ActionInfo";
    jsonValue["@odata.id"] = boost::urls::format(
        "/redfish/v1/Systems/{}/Oem/Nvidia/SetProcessorPowerLimitsActionInfo",
        systemId);
    jsonValue["Id"] = "SetProcessorPowerLimitsActionInfo";
    jsonValue["Name"] = "Set Processor Power Limits Action Info";
    jsonValue["Description"] =
        "Parameters for the SetProcessorPowerLimits OEM action";

    nlohmann::json::array_t parameters;
    nlohmann::json::object_t processorLimits;
    processorLimits["Name"] = "ProcessorLimits";
    processorLimits["Required"] = true;
    processorLimits["DataType"] = "ObjectArray";
    processorLimits["ObjectDataType"] =
        "#NvidiaComputerSystem.v1_9_0.ProcessorPowerLimitEntry";
    parameters.emplace_back(std::move(processorLimits));

    jsonValue["Parameters"] = std::move(parameters);
}

// Advertises the SetProcessorPowerLimits OEM action under Actions.Oem of the
// ComputerSystem resource, with its @Redfish.ActionInfo pointer. Extracted out
// of upstream systems.hpp so the NVIDIA-specific advertisement payload lives in
// the NVIDIA-specific file and systems.hpp calls only this one function. The
// BMCWEB_NVIDIA_OEM_PROPERTIES gating remains at the systems.hpp call site.
inline void advertiseSetProcessorPowerLimits(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    std::string_view systemId)
{
    asyncResp->res
        .jsonValue["Actions"]["Oem"]
                  ["#NvidiaComputerSystem.SetProcessorPowerLimits"]["target"] =
        boost::urls::format("/redfish/v1/Systems/{}/Actions/Oem/"
                            "NvidiaComputerSystem.SetProcessorPowerLimits",
                            systemId);
    asyncResp->res.jsonValue["Actions"]["Oem"]
                            ["#NvidiaComputerSystem.SetProcessorPowerLimits"]
                            ["@Redfish.ActionInfo"] =
        boost::urls::format("/redfish/v1/Systems/{}/Oem/Nvidia/"
                            "SetProcessorPowerLimitsActionInfo",
                            systemId);
}

} // namespace nvidia_aggregate_power

inline void requestRoutesProcessorPowerLimits(App& app)
{
    BMCWEB_ROUTE(
        app,
        "/redfish/v1/Systems/<str>/Actions/Oem/NvidiaComputerSystem.SetProcessorPowerLimits/")
        .privileges(redfish::privileges::patchComputerSystem)
        .methods(boost::beast::http::verb::post)(std::bind_front(
            nvidia_aggregate_power::handleSetProcessorPowerLimitsPost,
            std::ref(app)));

    BMCWEB_ROUTE(
        app,
        "/redfish/v1/Systems/<str>/Oem/Nvidia/SetProcessorPowerLimitsActionInfo/")
        .privileges(redfish::privileges::getActionInfo)
        .methods(boost::beast::http::verb::get)(std::bind_front(
            nvidia_aggregate_power::handleSetProcessorPowerLimitsActionInfoGet,
            std::ref(app)));
}

} // namespace redfish
