// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES.
// All rights reserved.
#pragma once

#include "bmcweb_config.h"

#include "app.hpp"
#include "async_resp.hpp"
#include "dbus_utility.hpp"
#include "error_messages.hpp"
#include "logging.hpp"
#include "query.hpp"
#include "registries/privilege_registry.hpp"
#include "utils/chassis_utils.hpp"

#include <boost/system/error_code.hpp>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <format>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// OEM transceiver resources.
//
// A chassis that hosts pluggable optical modules exposes them as
// Oem/Nvidia/Transceivers/<id>. The members are the inventory objects the
// backend associates with the chassis through its all_transceivers
// association; <id> is the object name. A transceiver object carries the
// module Asset decorator, its presence as OperationalStatus (Enabled or
// Absent) with an Availability decorator, the per-lane
// PortOpticalModuleMetrics and the OpticalLaneStatus flags. Two further
// objects, associated to the transceiver as module_state and power_good,
// carry the CMIS operating state with its PowerState decorator and the cage
// power-good state. Nothing here is derived from object path names.
namespace redfish::nvidia_transceiver
{
using Properties = dbus::utility::DBusPropertiesMap;

constexpr std::string_view mapperService = "xyz.openbmc_project.ObjectMapper";
constexpr std::string_view associationInterface =
    "xyz.openbmc_project.Association";
// The backend publishes no form-factor property yet; every carrier the
// transceiver objects describe today is an OSFP cage.
constexpr std::string_view moduleType = "OSFP";

template <typename T>
const T* property(const Properties& values, std::string_view key)
{
    for (const auto& [name, value] : values)
    {
        if (name == key)
        {
            return std::get_if<T>(&value);
        }
    }
    return nullptr;
}

inline std::string enumValue(const Properties& values, std::string_view key)
{
    const auto* value = property<std::string>(values, key);
    if (value == nullptr)
    {
        return {};
    }
    return value->substr(value->find_last_of('.') + 1);
}

inline bool available(const Properties& values)
{
    const bool* value = property<bool>(values, "Available");
    return value == nullptr || *value;
}

inline std::string collectionUri(std::string_view chassisId)
{
    return std::format("/redfish/v1/Chassis/{}/Oem/Nvidia/Transceivers",
                       chassisId);
}

inline std::string displayName(std::string_view chassisId, std::string_view id)
{
    std::string name = std::format("{} {}", chassisId, id);
    std::ranges::replace(name, '_', ' ');
    return name;
}

// Last element of an object path: the Id of the resource it backs.
inline std::string objectName(const std::string& path)
{
    const size_t slash = path.find_last_of('/');
    return slash == std::string::npos ? path : path.substr(slash + 1);
}

// Endpoints of |path|/|association|; an association the mapper does not know
// is an empty list.
inline void readEndpoints(
    const std::string& path, std::string_view association,
    std::function<void(const std::vector<std::string>&)>&& complete)
{
    dbus::utility::getProperty<std::vector<std::string>>(
        std::string(mapperService), std::format("{}/{}", path, association),
        std::string(associationInterface), "endpoints",
        [complete =
             std::move(complete)](const boost::system::error_code& ec,
                                  const std::vector<std::string>& endpoints) {
            if (ec)
            {
                complete({});
                return;
            }
            complete(endpoints);
        });
}

struct ObjectRead
{
    std::shared_ptr<bmcweb::AsyncResp> asyncResp;
    Properties properties;
    size_t pending = 0;
    bool failed = false;
    std::function<void(const Properties&)> complete;
};

inline void afterObjectProperties(const std::shared_ptr<ObjectRead>& read,
                                  const boost::system::error_code& ec,
                                  const Properties& properties)
{
    if (read->failed)
    {
        return;
    }
    if (ec)
    {
        BMCWEB_LOG_ERROR("Transceiver property read failed: {}", ec);
        read->failed = true;
        messages::internalError(read->asyncResp->res);
        return;
    }
    read->properties.insert(read->properties.end(), properties.begin(),
                            properties.end());
    if (--read->pending == 0)
    {
        read->complete(read->properties);
    }
}

// The properties of every interface of every service on |path|, merged. An
// object the mapper does not know completes with no properties. The backend
// uses distinct property names for the decorators of one module.
inline void readObject(const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                       const std::string& path,
                       std::function<void(const Properties&)>&& complete)
{
    dbus::utility::getDbusObject(
        path, std::array<std::string_view, 0>{},
        [asyncResp, path, complete = std::move(complete)](
            const boost::system::error_code& ec,
            const dbus::utility::MapperGetObject& object) mutable {
            if (ec || object.empty())
            {
                complete({});
                return;
            }
            // The mapper lists itself with no interfaces when it holds
            // association objects below the path; only services that
            // implement something on the object are read.
            std::vector<std::string> services;
            for (const auto& [service, interfaces] : object)
            {
                if (!interfaces.empty() && service != mapperService)
                {
                    services.push_back(service);
                }
            }
            if (services.empty())
            {
                complete({});
                return;
            }
            auto read = std::make_shared<ObjectRead>();
            read->asyncResp = asyncResp;
            read->complete = std::move(complete);
            read->pending = services.size();
            for (const std::string& service : services)
            {
                dbus::utility::getAllProperties(
                    service, path, "",
                    std::bind_front(afterObjectProperties, read));
            }
        });
}

struct Module
{
    std::string path;
    Properties transceiver;
    Properties moduleState;
    Properties powerGood;
};

// The transceiver object and the two state objects associated with it.
inline void readModule(const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                       const std::string& path,
                       std::function<void(const Module&)>&& complete)
{
    auto item = std::make_shared<Module>();
    item->path = path;
    readObject(
        asyncResp, path,
        [asyncResp, item, complete = std::move(complete)](
            const Properties& transceiver) mutable {
            item->transceiver = transceiver;
            readEndpoints(
                item->path, "module_state",
                [asyncResp, item, complete = std::move(complete)](
                    const std::vector<std::string>& states) mutable {
                    auto afterState = [asyncResp, item,
                                       complete = std::move(complete)](
                                          const Properties& state) mutable {
                        item->moduleState = state;
                        readEndpoints(
                            item->path, "power_good",
                            [asyncResp, item, complete = std::move(complete)](
                                const std::vector<std::string>& goods) mutable {
                                auto afterGood =
                                    [item, complete = std::move(complete)](
                                        const Properties& good) {
                                        item->powerGood = good;
                                        complete(*item);
                                    };
                                if (goods.empty())
                                {
                                    afterGood({});
                                    return;
                                }
                                readObject(asyncResp, goods.front(),
                                           std::move(afterGood));
                            });
                    };
                    if (states.empty())
                    {
                        afterState({});
                        return;
                    }
                    readObject(asyncResp, states.front(),
                               std::move(afterState));
                });
        });
}

// Presence and cage power are independent of CMIS readiness. An unknown
// power sample must not turn into a healthy module.
inline nlohmann::json status(const Module& item)
{
    if (item.transceiver.empty())
    {
        return {{"State", "UnavailableOffline"}};
    }
    const std::string presence = enumValue(item.transceiver, "State");
    if (!available(item.transceiver) ||
        (presence != "Enabled" && presence != "Absent"))
    {
        return {{"State", "UnavailableOffline"}};
    }
    if (presence == "Absent")
    {
        return {{"State", "Absent"}};
    }
    const std::string power = enumValue(item.powerGood, "State");
    if (available(item.powerGood) && power == "Enabled")
    {
        return {{"State", "Enabled"}, {"Health", "OK"}};
    }
    nlohmann::json result = {{"State", "UnavailableOffline"}};
    if (available(item.powerGood) && power == "Fault")
    {
        result["Health"] = "Critical";
    }
    return result;
}

inline nlohmann::json powerState(const Module& item)
{
    if (!available(item.moduleState))
    {
        return nullptr;
    }
    std::string state = enumValue(item.moduleState, "PowerState");
    if (state == "On" || state == "Off" || state == "PoweringOn" ||
        state == "PoweringOff")
    {
        return state;
    }
    return nullptr;
}

inline bool moduleReady(const Module& item)
{
    return available(item.transceiver) &&
           enumValue(item.transceiver, "State") == "Enabled" &&
           available(item.moduleState) &&
           enumValue(item.moduleState, "State") == "Enabled";
}

inline nlohmann::json laneNumber(const std::vector<double>* values, size_t lane)
{
    if (values == nullptr || lane >= values->size() ||
        !std::isfinite((*values)[lane]))
    {
        return nullptr;
    }
    return (*values)[lane];
}

inline nlohmann::json laneFlag(const std::vector<std::string>* values,
                               size_t lane)
{
    if (values == nullptr || lane >= values->size())
    {
        return nullptr;
    }
    const std::string& value = (*values)[lane];
    if (value.ends_with(".Asserted"))
    {
        return true;
    }
    if (value.ends_with(".Clear"))
    {
        return false;
    }
    return nullptr;
}

// Per-lane metrics while the module decodes CMIS; an empty list otherwise.
inline nlohmann::json lanes(const Module& item)
{
    nlohmann::json result = nlohmann::json::array();
    if (!moduleReady(item))
    {
        return result;
    }
    const auto* tx = property<std::vector<double>>(item.transceiver,
                                                   "TXOutputPowerMilliWatts");
    const auto* rx = property<std::vector<double>>(item.transceiver,
                                                   "RXInputPowerMilliWatts");
    const auto* bias = property<std::vector<double>>(item.transceiver,
                                                     "TXBiasCurrentMilliAmps");
    const auto* rxLos =
        property<std::vector<std::string>>(item.transceiver, "RxLossOfSignal");
    const auto* txFault =
        property<std::vector<std::string>>(item.transceiver, "TxFault");
    size_t count = 0;
    for (size_t size : {tx == nullptr ? size_t{0} : tx->size(),
                        rx == nullptr ? size_t{0} : rx->size(),
                        bias == nullptr ? size_t{0} : bias->size(),
                        rxLos == nullptr ? size_t{0} : rxLos->size(),
                        txFault == nullptr ? size_t{0} : txFault->size()})
    {
        count = std::max(count, size);
    }
    for (size_t lane = 0; lane < count; ++lane)
    {
        result.push_back({{"LaneId", lane},
                          {"TXOutputPowerMilliWatts", laneNumber(tx, lane)},
                          {"RXInputPowerMilliWatts", laneNumber(rx, lane)},
                          {"TXBiasCurrentMilliAmps", laneNumber(bias, lane)},
                          {"RxLossOfSignal", laneFlag(rxLos, lane)},
                          {"TxFault", laneFlag(txFault, lane)}});
    }
    return result;
}

inline void renderTransceiver(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& chassisId, const std::string& id, bool metrics,
    const Module& item)
{
    const std::string uri = std::format("{}/{}", collectionUri(chassisId), id);
    const std::string name = displayName(chassisId, id);
    nlohmann::json& json = asyncResp->res.jsonValue;
    if (metrics)
    {
        json["@odata.type"] =
            "#NvidiaTransceiverMetrics.v1_0_0.NvidiaTransceiverMetrics";
        json["@odata.id"] = uri + "/Metrics";
        json["Id"] = "Metrics";
        json["Name"] = name + " Transceiver Metrics";
        json["Lanes"] = lanes(item);
        return;
    }
    json["@odata.type"] = "#NvidiaTransceiver.v1_0_0.NvidiaTransceiver";
    json["@odata.id"] = uri;
    json["Id"] = id;
    json["Name"] = name;
    for (std::string_view field : {"SerialNumber", "PartNumber"})
    {
        const auto* value = property<std::string>(item.transceiver, field);
        if (value != nullptr && !value->empty())
        {
            json[field] = *value;
        }
    }
    json["Type"] = std::string(moduleType);
    json["Status"] = status(item);
    json["PowerState"] = powerState(item);
    json["Metrics"]["@odata.id"] = uri + "/Metrics";
}

inline void renderCollection(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& chassisId, std::vector<std::string> transceivers)
{
    std::ranges::sort(transceivers);
    nlohmann::json& json = asyncResp->res.jsonValue;
    json["@odata.type"] =
        "#NvidiaTransceiverCollection.NvidiaTransceiverCollection";
    json["@odata.id"] = collectionUri(chassisId);
    json["Name"] = std::format("{} {} Transceivers", chassisId, moduleType);
    nlohmann::json& members = json["Members"];
    members = nlohmann::json::array();
    for (const std::string& path : transceivers)
    {
        const std::string id = objectName(path);
        if (id.empty())
        {
            continue;
        }
        members.push_back(
            {{"@odata.id",
              std::format("{}/{}", collectionUri(chassisId), id)}});
    }
    json["Members@odata.count"] = members.size();
}

inline void afterChassisTransceivers(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& chassisId, const std::string& id, bool metrics,
    const std::vector<std::string>& transceivers)
{
    if (transceivers.empty())
    {
        messages::resourceNotFound(
            asyncResp->res,
            id.empty() ? "NvidiaTransceiverCollection" : "NvidiaTransceiver",
            id.empty() ? chassisId : id);
        return;
    }
    if (id.empty())
    {
        renderCollection(asyncResp, chassisId, transceivers);
        return;
    }
    const auto found =
        std::ranges::find_if(transceivers, [&id](const std::string& path) {
            return objectName(path) == id;
        });
    if (found == transceivers.end())
    {
        messages::resourceNotFound(asyncResp->res, "NvidiaTransceiver", id);
        return;
    }
    readModule(
        asyncResp, *found,
        std::bind_front(renderTransceiver, asyncResp, chassisId, id, metrics));
}

inline void afterValidChassisPath(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& chassisId, const std::string& id, bool metrics,
    const std::optional<std::string>& chassisPath)
{
    if (!chassisPath)
    {
        messages::resourceNotFound(asyncResp->res, "Chassis", chassisId);
        return;
    }
    readEndpoints(*chassisPath, "all_transceivers",
                  std::bind_front(afterChassisTransceivers, asyncResp,
                                  chassisId, id, metrics));
}

inline void handleTransceiverGet(
    App& app, const crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& chassisId, const std::string& id, bool metrics)
{
    if (!setUpRedfishRoute(app, req, asyncResp))
    {
        return;
    }
    chassis_utils::getValidChassisPath(
        asyncResp, chassisId,
        std::bind_front(afterValidChassisPath, asyncResp, chassisId, id,
                        metrics));
}

// Links a chassis to its transceiver collection when the backend associates
// transceivers with it. Called by the common chassis handler with the chassis
// object it found; the OEM type finalizer selects the schema version from
// the presence of the link.
inline void addChassisLink(const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                           const std::string& chassisId,
                           const std::string& chassisPath)
{
    if constexpr (!(BMCWEB_NVIDIA_FPGA_TELEMETRY &&
                    BMCWEB_NVIDIA_OEM_PROPERTIES))
    {
        return;
    }
    readEndpoints(
        chassisPath, "all_transceivers",
        [asyncResp, chassisId](const std::vector<std::string>& transceivers) {
            if (transceivers.empty())
            {
                return;
            }
            asyncResp->res
                .jsonValue["Oem"]["Nvidia"]["Transceivers"]["@odata.id"] =
                collectionUri(chassisId);
        });
}

inline void requestRoutes(App& app)
{
    if constexpr (!(BMCWEB_NVIDIA_FPGA_TELEMETRY &&
                    BMCWEB_NVIDIA_OEM_PROPERTIES))
    {
        return;
    }
    BMCWEB_ROUTE(app, "/redfish/v1/Chassis/<str>/Oem/Nvidia/Transceivers/")
        .privileges(redfish::privileges::getChassis)
        .methods(boost::beast::http::verb::get)(
            [&app](const crow::Request& req,
                   const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                   const std::string& chassisId) {
                handleTransceiverGet(app, req, asyncResp, chassisId, {}, false);
            });
    BMCWEB_ROUTE(app,
                 "/redfish/v1/Chassis/<str>/Oem/Nvidia/Transceivers/<str>/")
        .privileges(redfish::privileges::getChassis)
        .methods(boost::beast::http::verb::get)(
            [&app](const crow::Request& req,
                   const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                   const std::string& chassisId, const std::string& id) {
                handleTransceiverGet(app, req, asyncResp, chassisId, id, false);
            });
    BMCWEB_ROUTE(
        app, "/redfish/v1/Chassis/<str>/Oem/Nvidia/Transceivers/<str>/Metrics/")
        .privileges(redfish::privileges::getChassis)
        .methods(boost::beast::http::verb::get)(
            [&app](const crow::Request& req,
                   const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                   const std::string& chassisId, const std::string& id) {
                handleTransceiverGet(app, req, asyncResp, chassisId, id, true);
            });
}
} // namespace redfish::nvidia_transceiver
