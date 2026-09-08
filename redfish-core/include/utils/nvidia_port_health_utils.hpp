/*
 * SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION &
 * AFFILIATES. All rights reserved. SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#include "async_resp.hpp"
#include "dbus_utility.hpp"
#include "error_message_utils.hpp"
#include "error_messages.hpp"
#include "generated/enums/nvidia_port_metrics.hpp"
#include "http_response.hpp"
#include "logging.hpp"
#include "parsing.hpp"
#include "utils/nvidia_async_call_utils.hpp"

#include <asm-generic/errno.h>

#include <boost/beast/http/status.hpp>
#include <boost/system/error_code.hpp>
#include <nlohmann/json.hpp>
#include <sdbusplus/message/native_types.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <variant>

namespace redfish
{
namespace nvidia_port_health_utils
{

// Producer contract: com.nvidia.NVLink.PortHealthMetrics, published by nsmd on
// every NVLink-family port object (phosphor-dbus-interfaces).
constexpr std::string_view portHealthMetricsInterface =
    "com.nvidia.NVLink.PortHealthMetrics";
constexpr std::string_view clearEarlyHealthIndicationMethod =
    "ClearEarlyHealthIndication";
constexpr std::string_view clearEarlyHealthIndicationSupportedProperty =
    "ClearEarlyHealthIndicationSupported";
constexpr std::string_view earlyHealthIndicationValuesPrefix =
    "com.nvidia.NVLink.PortHealthMetrics.EarlyHealthIndicationValues.";
constexpr std::string_view attentionTriggerReasonValuesPrefix =
    "com.nvidia.NVLink.PortHealthMetrics.AttentionTriggerReasonValues.";
constexpr std::string_view attentionTriggerConfigurationValuesPrefix =
    "com.nvidia.NVLink.PortHealthMetrics.AttentionTriggerConfigurationValues.";

// Redfish surface: NvidiaPortMetrics.v1_10_0 from the bundled OEM schema,
// shared by the GPU (Systems/../Processors/../Ports/../Metrics) and NVSwitch
// (Fabrics/../Switches/../Ports/../Metrics) PortMetrics resources.
constexpr std::string_view nvlinkPortMetricsOdataType =
    "#NvidiaPortMetrics.v1_10_0.NvidiaNVLinkPortMetrics";
// Key of the action in Actions.Oem.
constexpr std::string_view clearEarlyHealthIndicationActionKey =
    "#NvidiaPortMetrics.ClearEarlyHealthIndication";
// Action name as it appears in Redfish messages (bare, no '#').
constexpr std::string_view clearEarlyHealthIndicationActionName =
    "NvidiaPortMetrics.ClearEarlyHealthIndication";
constexpr std::string_view clearEarlyHealthIndicationActionPath =
    "/Actions/Oem/NvidiaPortMetrics.ClearEarlyHealthIndication";

// Ceiling for the device outcome of a clear. The async helper answers
// OperationTimeout (served as 500) when no terminal Async.Status arrives in
// time; InProgress keeps the poll alive.
constexpr std::chrono::seconds clearEarlyHealthIndicationTimeout{60};

// Retry-After sent with ServiceTemporarilyUnavailable (503) when nsmd reports
// the device busy or not ready, or its async result pool exhausted.
constexpr std::string_view clearEarlyHealthIndicationRetryAfter = "60";

/**
 * @brief Map a D-Bus enum member string onto a generated Redfish enum.
 *
 * Strips the interface-qualified prefix and lets the generated
 * NLOHMANN_JSON_SERIALIZE_ENUM table decide membership, so only members the
 * bundled schema declares can ever be emitted.
 *
 * @return EnumType::Invalid for a value outside the prefix or the schema.
 */
template <typename EnumType>
EnumType dbusMemberToEnum(std::string_view dbusValue, std::string_view prefix)
{
    if (!dbusValue.starts_with(prefix))
    {
        return EnumType::Invalid;
    }
    nlohmann::json member = std::string(dbusValue.substr(prefix.size()));
    return member.get<EnumType>();
}

/**
 * @brief EarlyHealthIndication D-Bus value to Redfish enum.
 *
 * @return std::nullopt for the D-Bus-only member Unavailable (the device
 *         reports the record unreadable): the caller renders null for every
 *         health property so no stale value is ever returned.
 *         EarlyHealthIndication::Invalid for an unmappable value: the caller
 *         omits the property rather than inventing a value.
 */
inline std::optional<nvidia_port_metrics::EarlyHealthIndication>
    toEarlyHealthIndication(std::string_view dbusValue)
{
    if (dbusValue.starts_with(earlyHealthIndicationValuesPrefix) &&
        dbusValue.substr(earlyHealthIndicationValuesPrefix.size()) ==
            "Unavailable")
    {
        return std::nullopt;
    }
    return dbusMemberToEnum<nvidia_port_metrics::EarlyHealthIndication>(
        dbusValue, earlyHealthIndicationValuesPrefix);
}

/**
 * @brief AttentionTriggerReason D-Bus value to Redfish enum.
 *
 * The D-Bus Unknown member and any value outside the schema both render as
 * Unknown, so the result is always emittable.
 */
inline nvidia_port_metrics::AttentionTriggerReason toAttentionTriggerReason(
    std::string_view dbusValue)
{
    nvidia_port_metrics::AttentionTriggerReason reason =
        dbusMemberToEnum<nvidia_port_metrics::AttentionTriggerReason>(
            dbusValue, attentionTriggerReasonValuesPrefix);
    if (reason == nvidia_port_metrics::AttentionTriggerReason::Invalid)
    {
        return nvidia_port_metrics::AttentionTriggerReason::Unknown;
    }
    return reason;
}

/**
 * @brief AttentionTriggerConfiguration D-Bus value to Redfish enum.
 *
 * The D-Bus Unknown member and any value outside the schema both render as
 * Unknown, so the result is always emittable.
 */
inline nvidia_port_metrics::AttentionTriggerConfiguration
    toAttentionTriggerConfiguration(std::string_view dbusValue)
{
    nvidia_port_metrics::AttentionTriggerConfiguration configuration =
        dbusMemberToEnum<nvidia_port_metrics::AttentionTriggerConfiguration>(
            dbusValue, attentionTriggerConfigurationValuesPrefix);
    if (configuration ==
        nvidia_port_metrics::AttentionTriggerConfiguration::Invalid)
    {
        return nvidia_port_metrics::AttentionTriggerConfiguration::Unknown;
    }
    return configuration;
}

/**
 * @brief Render the NvidiaPortMetrics.v1_10_0 link health block and, when the
 *        device supports it, the ClearEarlyHealthIndication action.
 *
 * Consumes the GetAll property map of a port object. A port that does not
 * publish com.nvidia.NVLink.PortHealthMetrics (no EarlyHealthIndication
 * property) is left untouched, so PCIe and network ports carry no health
 * properties. The caller has already set @odata.id and the Oem.Nvidia
 * @odata.type of the resource.
 *
 * @param[in,out] asyncResp   Async HTTP response.
 * @param[in]     properties  GetAll result for the port object.
 */
inline void populatePortHealthMetrics(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const dbus::utility::DBusPropertiesMap& properties)
{
    bool earlyHealthIndicationPresent = false;
    const std::string* earlyHealthIndication = nullptr;
    const std::string* attentionTriggerReason = nullptr;
    const uint8_t* attentionTriggerMetricId = nullptr;
    const std::string* attentionTriggerConfiguration = nullptr;
    const bool* clearSupported = nullptr;

    for (const auto& [name, value] : properties)
    {
        if (name == "EarlyHealthIndication")
        {
            earlyHealthIndicationPresent = true;
            earlyHealthIndication = std::get_if<std::string>(&value);
        }
        else if (name == "AttentionTriggerReason")
        {
            attentionTriggerReason = std::get_if<std::string>(&value);
        }
        else if (name == "AttentionTriggerMetricId")
        {
            // Link health agent metric slot index, 1..15; 0 means none.
            attentionTriggerMetricId = std::get_if<uint8_t>(&value);
        }
        else if (name == "AttentionTriggerConfiguration")
        {
            attentionTriggerConfiguration = std::get_if<std::string>(&value);
        }
        else if (name == clearEarlyHealthIndicationSupportedProperty)
        {
            clearSupported = std::get_if<bool>(&value);
        }
    }

    if (!earlyHealthIndicationPresent)
    {
        // Interface absent: nothing to render.
        return;
    }
    if (earlyHealthIndication == nullptr)
    {
        // Producer contract break (not a string). Log it, but do not fail the
        // standard PortMetrics resource over an optional OEM block.
        BMCWEB_LOG_ERROR(
            "EarlyHealthIndication is not a string; link health block omitted");
        return;
    }

    nlohmann::json& oemNvidia = asyncResp->res.jsonValue["Oem"]["Nvidia"];

    std::optional<nvidia_port_metrics::EarlyHealthIndication> health =
        toEarlyHealthIndication(*earlyHealthIndication);
    if (!health)
    {
        // Record unreadable on the device: null for all four, never stale.
        oemNvidia["EarlyHealthIndication"] = nullptr;
        oemNvidia["AttentionTriggerReason"] = nullptr;
        oemNvidia["AttentionTriggerMetricId"] = nullptr;
        oemNvidia["AttentionTriggerConfiguration"] = nullptr;
    }
    else
    {
        const bool inAttention =
            *health == nvidia_port_metrics::EarlyHealthIndication::Attention;
        if (*health != nvidia_port_metrics::EarlyHealthIndication::Invalid)
        {
            oemNvidia["EarlyHealthIndication"] = *health;
        }
        if (attentionTriggerReason != nullptr)
        {
            oemNvidia["AttentionTriggerReason"] =
                toAttentionTriggerReason(*attentionTriggerReason);
        }
        // Metric slot index: an integer 1..15 while in Attention, otherwise
        // null (0 and reserved values mean none or not available).
        if (inAttention && attentionTriggerMetricId != nullptr &&
            *attentionTriggerMetricId >= 1 && *attentionTriggerMetricId <= 15)
        {
            oemNvidia["AttentionTriggerMetricId"] = *attentionTriggerMetricId;
        }
        else
        {
            oemNvidia["AttentionTriggerMetricId"] = nullptr;
        }
        // Configuration indicator: Current or Modified while in Attention,
        // Unknown otherwise (also for producers that predate the property).
        nvidia_port_metrics::AttentionTriggerConfiguration configuration =
            nvidia_port_metrics::AttentionTriggerConfiguration::Unknown;
        if (inAttention && attentionTriggerConfiguration != nullptr)
        {
            configuration =
                toAttentionTriggerConfiguration(*attentionTriggerConfiguration);
        }
        oemNvidia["AttentionTriggerConfiguration"] = configuration;
    }

    // The clear is a device capability: advertise the action only when the
    // producer says so, not on interface presence alone (producers that
    // predate ClearEarlyHealthIndicationSupported keep the interface but
    // cannot clear).
    if (clearSupported == nullptr || !*clearSupported)
    {
        return;
    }
    const std::string* odataId =
        asyncResp->res.jsonValue["@odata.id"].get_ptr<const std::string*>();
    if (odataId == nullptr)
    {
        BMCWEB_LOG_ERROR("PortMetrics @odata.id missing; cannot advertise {}",
                         clearEarlyHealthIndicationActionKey);
        return;
    }
    asyncResp->res.jsonValue["Actions"]["Oem"][std::string(
        clearEarlyHealthIndicationActionKey)]["target"] =
        *odataId + std::string(clearEarlyHealthIndicationActionPath);
}

/**
 * @brief Reject action parameters: ClearEarlyHealthIndication takes none.
 *
 * Accepts a zero-length body or an empty JSON object, so a bare POST works
 * like the sibling parameterless OEM actions (SetRecoveryMode, L1Reset).
 * Any member is rejected with ActionParameterUnknown naming it; malformed or
 * non-object JSON is rejected too. Populates the error response and returns
 * false on rejection.
 */
inline bool readClearEarlyHealthIndicationBody(std::string_view body,
                                               crow::Response& res)
{
    if (body.empty())
    {
        return true;
    }
    std::optional<nlohmann::json> actionParams = parseStringAsJson(std::string(body));
    if (!actionParams)
    {
        messages::malformedJSON(res);
        return false;
    }
    const nlohmann::json::object_t* object =
        actionParams->get_ptr<const nlohmann::json::object_t*>();
    if (object == nullptr)
    {
        messages::unrecognizedRequestBody(res);
        return false;
    }
    if (!object->empty())
    {
        messages::actionParameterUnknown(
            res, clearEarlyHealthIndicationActionName, object->begin()->first);
        return false;
    }
    return true;
}

/**
 * @brief Map the terminal Async.Status of a clear onto HTTP.
 *
 * InProgress never reaches here: the async helper keeps polling until a
 * terminal status or its ceiling, which it reports as OperationTimeout.
 */
inline void afterClearEarlyHealthIndication(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& portPath, const std::string& status)
{
    if (status == nvidia_async_operation_utils::asyncStatusValueSuccess)
    {
        BMCWEB_LOG_DEBUG("Cleared early health indication on {}", portPath);
        messages::success(asyncResp->res);
        return;
    }
    if (status ==
        nvidia_async_operation_utils::asyncStatusValueUnsupportedRequest)
    {
        // The device firmware does not implement the clear.
        BMCWEB_LOG_WARNING(
            "Clear early health indication not supported by the device behind {}",
            portPath);
        messages::actionNotSupported(asyncResp->res,
                                     clearEarlyHealthIndicationActionName);
        return;
    }
    if (status == nvidia_async_operation_utils::asyncStatusValueUnavailable)
    {
        // Device busy or not ready, or the nsmd result pool is exhausted
        // (Common.Error.Unavailable is folded into this status by the helper).
        BMCWEB_LOG_WARNING(
            "Clear early health indication on {} unavailable; retry later",
            portPath);
        messages::serviceTemporarilyUnavailable(
            asyncResp->res, clearEarlyHealthIndicationRetryAfter);
        return;
    }
    if (status == nvidia_async_operation_utils::asyncStatusValueTimeout)
    {
        BMCWEB_LOG_ERROR("Clear early health indication on {} timed out",
                         portPath);
        messages::operationTimeout(asyncResp->res);
        return;
    }
    if (status ==
        nvidia_async_operation_utils::asyncStatusValueConflictingOperation)
    {
        // Another operation on this port is still in flight. 409 per the
        // DSP0266 status-code table (conflict with the current state of the
        // resource) with the Base.ResourceInUse error body: DSP0266 8.3 asks
        // for an extended error on every 4XX/5XX. messages::resourceInUse(res)
        // is not used because it fixes the status at 503.
        BMCWEB_LOG_WARNING(
            "Clear early health indication on {} rejected: conflicting operation",
            portPath);
        asyncResp->res.result(boost::beast::http::status::conflict);
        messages::addMessageToErrorJson(asyncResp->res.jsonValue,
                                        messages::resourceInUse());
        return;
    }
    if (status ==
        nvidia_async_operation_utils::asyncStatusValueResourceNotFound)
    {
        // The helper folds D-Bus UnknownObject/UnknownMethod into this status:
        // the port object vanished between the lookup and the call.
        BMCWEB_LOG_WARNING("Port object {} vanished before the clear",
                           portPath);
        messages::resourceNotFound(asyncResp->res, "Port",
                                   sdbusplus::message::object_path(portPath).filename());
        return;
    }
    // WriteFailure, InvalidArgument, InternalFailure and anything unmapped:
    // the device rejected the request or it could not be encoded/decoded.
    BMCWEB_LOG_ERROR("Clear early health indication on {} failed: {}", portPath,
                     status);
    messages::internalError(asyncResp->res);
}

inline void doClearEarlyHealthIndication(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& service, const std::string& portPath)
{
    BMCWEB_LOG_DEBUG("Clearing early health indication on {} via {}", portPath,
                     service);
    nvidia_async_operation_utils::doGenericCallAsyncAndGatherResult<void>(
        asyncResp, clearEarlyHealthIndicationTimeout, service, portPath,
        std::string(portHealthMetricsInterface),
        std::string(clearEarlyHealthIndicationMethod),
        [asyncResp, portPath](const std::string& status) {
            afterClearEarlyHealthIndication(asyncResp, portPath, status);
        });
}

inline void afterGetClearEarlyHealthIndicationSupported(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& service, const std::string& portPath,
    const boost::system::error_code& ec, bool clearSupported)
{
    if (ec)
    {
        if (ec.value() == EBADR)
        {
            // Interface present but the property is absent: the producer
            // predates ClearEarlyHealthIndicationSupported, so the port cannot
            // be cleared.
            BMCWEB_LOG_DEBUG("ClearEarlyHealthIndicationSupported absent on {}",
                             portPath);
            messages::actionNotSupported(asyncResp->res,
                                         clearEarlyHealthIndicationActionName);
            return;
        }
        BMCWEB_LOG_ERROR(
            "DBUS response error {} reading ClearEarlyHealthIndicationSupported on {}",
            ec, portPath);
        messages::internalError(asyncResp->res);
        return;
    }
    if (!clearSupported)
    {
        BMCWEB_LOG_DEBUG("Clear early health indication not supported on {}",
                         portPath);
        messages::actionNotSupported(asyncResp->res,
                                     clearEarlyHealthIndicationActionName);
        return;
    }
    doClearEarlyHealthIndication(asyncResp, service, portPath);
}

inline void afterGetPortObjectForClearEarlyHealthIndication(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& portPath, const boost::system::error_code& ec,
    const dbus::utility::MapperGetObject& object)
{
    if (ec)
    {
        BMCWEB_LOG_ERROR("ObjectMapper GetObject failed for {}: {}", portPath,
                         ec);
        messages::internalError(asyncResp->res);
        return;
    }
    for (const auto& [service, interfaces] : object)
    {
        if (std::ranges::find(interfaces, portHealthMetricsInterface) ==
            interfaces.end())
        {
            continue;
        }
        dbus::utility::getProperty<bool>(
            service, portPath, std::string(portHealthMetricsInterface),
            std::string(clearEarlyHealthIndicationSupportedProperty),
            std::bind_front(afterGetClearEarlyHealthIndicationSupported,
                            asyncResp, service, portPath));
        return;
    }
    // The port exists but publishes no link health (PCIe port, or a producer
    // without the interface): nothing to clear.
    BMCWEB_LOG_DEBUG("{} does not implement {}", portPath,
                     portHealthMetricsInterface);
    messages::actionNotSupported(asyncResp->res,
                                 clearEarlyHealthIndicationActionName);
}

/**
 * @brief POST ClearEarlyHealthIndication once the caller resolved the port.
 *
 * Shared by the GPU and NVSwitch routes. The caller has already answered 404
 * for an unknown system, processor, fabric, switch or port and rejected any
 * action parameter. Reads the ClearEarlyHealthIndicationSupported capability
 * first (false -> ActionNotSupported), then invokes the async method and maps
 * its outcome.
 *
 * @param[in,out] asyncResp  Async HTTP response.
 * @param[in]     portPath   D-Bus object path of the NVLink port.
 */
inline void clearEarlyHealthIndication(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& portPath)
{
    dbus::utility::getDbusObject(
        portPath, std::array<std::string_view, 0>{},
        std::bind_front(afterGetPortObjectForClearEarlyHealthIndication,
                        asyncResp, portPath));
}

} // namespace nvidia_port_health_utils
} // namespace redfish
