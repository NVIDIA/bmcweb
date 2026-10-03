// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES.
#pragma once

#include "async_resp.hpp"
#include "dbus_utility.hpp"
#include "error_messages.hpp"
#include "http_request.hpp"
#include "utils/json_utils.hpp"

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <tuple>
#include <vector>

namespace redfish::lpu_power_mode
{
inline constexpr std::string_view chassisId = "HGX_Chassis_0";
inline constexpr std::string_view controlId = "TotalLPU_Power_0";
inline constexpr std::string_view path =
    "/xyz/openbmc_project/control/power/TotalLPU_Power_0";
inline constexpr std::string_view interface = "com.nvidia.LPUPowerMode";
inline constexpr std::string_view uri =
    "/redfish/v1/Chassis/HGX_Chassis_0/Controls/TotalLPU_Power_0";
inline constexpr std::string_view maxQProfile = "MaxQ";
inline constexpr std::string_view maxPProfile = "MaxP";
inline constexpr std::array<std::string_view, 2> supportedPowerModes = {
    maxQProfile, maxPProfile};
using PowerProfile = std::tuple<std::string, uint32_t, std::string>;
using PowerProfiles = std::vector<PowerProfile>;
using ProfileSnapshot = std::tuple<std::string, PowerProfiles, std::string>;

inline bool isSupportedPowerMode(std::string_view mode)
{
    return std::ranges::find(supportedPowerModes, mode) !=
           supportedPowerModes.end();
}

inline bool validProfiles(const PowerProfiles& profiles,
                          const std::string& status)
{
    if (status == "Unavailable")
    {
        return profiles.empty();
    }
    if ((status != "Tentative" && status != "Final") || profiles.size() != 2)
    {
        return false;
    }
    const auto& [qName, qWatts, qDescription] = profiles[0];
    const auto& [pName, pWatts, pDescription] = profiles[1];
    return qName == maxQProfile && pName == maxPProfile && qWatts > 0 &&
           pWatts > qWatts && !qDescription.empty() && !pDescription.empty();
}

inline bool matches(std::string_view chassis, std::string_view control)
{
    return chassis == chassisId && control == controlId;
}

// Keep this mapping local to the LPU control. Other platforms' generic
// ControlMode/PowerMode mappings have different semantics.
inline void populate(nlohmann::json& json, const std::string& mode,
                     const PowerProfiles& profiles = {})
{
    json = {{"@odata.type", "#Control.v1_3_0.Control"},
            {"@odata.id", uri},
            {"Id", controlId},
            {"Name", "System Power Control"},
            {"ControlType", "Power"},
            {"ControlMode", "Automatic"},
            {"PhysicalContext", "Accelerator"},
            {"SetPointUnits", "W"},
            {"SetPoint", nullptr}};
    auto& nvidia = json["Oem"]["Nvidia"];
    nvidia["@odata.type"] = "#NvidiaControl.v1_0_0.NvidiaControl";
    nvidia["PowerMode@Redfish.AllowableValues"] = supportedPowerModes;
    nvidia["PowerMode"] = nullptr;
    nvidia["PowerModeProfiles"] = nlohmann::json::array();
    for (const auto& [name, watts, description] : profiles)
    {
        nvidia["PowerModeProfiles"].push_back({{"PowerMode", name},
                                               {"MaxPowerWatts", watts},
                                               {"Description", description}});
        if (name == mode)
        {
            json["SetPoint"] = watts;
        }
    }
    json["Status"] = {{"Health", "Warning"}, {"State", "UnavailableOffline"}};
    if (isSupportedPowerMode(mode))
    {
        nvidia["PowerMode"] = mode;
        json["Status"] = {{"Health", "OK"}, {"State", "Enabled"}};
    }
    auto& related = json["RelatedItem"];
    related = nlohmann::json::array();
    for (size_t index = 0; index < 16; ++index)
    {
        related.push_back(
            {{"@odata.id",
              "/redfish/v1/Systems/HGX_Baseboard_0/Processors/LPU_" +
                  std::to_string(index)}});
    }
}

inline bool readPatch(nlohmann::json& input, crow::Response& response,
                      std::optional<std::string>& powerMode)
{
    std::optional<nlohmann::json> oem;
    std::optional<nlohmann::json> setPoint;
    std::optional<nlohmann::json> controlMode;
    if (!json_util::readJson(input, response, "Oem", oem, "SetPoint", setPoint,
                             "ControlMode", controlMode))
    {
        return false;
    }
    // Validate the entire request before calling the backend. A mixed PATCH
    // must not change the mode and then fail on an unsupported watt setpoint.
    if (setPoint)
    {
        messages::propertyNotWritable(response, "SetPoint");
        return false;
    }
    if (controlMode)
    {
        messages::propertyNotWritable(response, "ControlMode");
        return false;
    }
    if (!oem)
    {
        messages::emptyJSON(response);
        return false;
    }
    nlohmann::json nvidia;
    if (!json_util::readJson(*oem, response, "Nvidia", nvidia) ||
        !json_util::readJson(nvidia, response, "PowerMode", powerMode))
    {
        return false;
    }
    if (!powerMode)
    {
        messages::propertyMissing(response, "Oem/Nvidia/PowerMode");
        return false;
    }
    if (!isSupportedPowerMode(*powerMode))
    {
        messages::propertyValueNotInList(response, *powerMode,
                                         "Oem/Nvidia/PowerMode");
        return false;
    }
    return true;
}

inline void withService(const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                        const std::optional<std::string>& chassisPath,
                        const std::function<void(const std::string&)>& callback)
{
    if (!chassisPath)
    {
        messages::resourceNotFound(asyncResp->res, "Chassis", chassisId);
        return;
    }
    // Existence requires BOTH the chassis association and the actual provider.
    // A matching URI alone must not fabricate a control on another platform.
    dbus::utility::getProperty<std::vector<std::string>>(
        "xyz.openbmc_project.ObjectMapper", *chassisPath + "/power_controls",
        "xyz.openbmc_project.Association", "endpoints",
        [asyncResp, callback](const boost::system::error_code& ec,
                              const std::vector<std::string>& endpoints) {
            if (ec || std::ranges::find(endpoints, path) == endpoints.end())
            {
                messages::resourceNotFound(asyncResp->res, "Control",
                                           controlId);
                return;
            }
            dbus::utility::getDbusObject(
                std::string(path), std::array<std::string_view, 1>{interface},
                [asyncResp,
                 callback](const boost::system::error_code& error,
                           const dbus::utility::MapperGetObject& obj) {
                    if (error || obj.empty())
                    {
                        messages::resourceNotFound(asyncResp->res, "Control",
                                                   controlId);
                        return;
                    }
                    if (obj.size() != 1)
                    {
                        messages::internalError(asyncResp->res);
                        return;
                    }
                    callback(obj.front().first);
                });
        });
}

inline void completeGet(
    crow::Response& response, const boost::system::error_code& ec,
    const std::string& mode, const PowerProfiles& profiles = {},
    const std::string& profileStatus = "Unavailable")
{
    if (ec)
    {
        messages::serviceTemporarilyUnavailable(response, "5");
        return;
    }
    if ((!mode.empty() && !isSupportedPowerMode(mode)) ||
        !validProfiles(profiles, profileStatus))
    {
        messages::internalError(response);
        return;
    }
    populate(response.jsonValue, mode, profiles);
}

inline void completeSet(crow::Response& response,
                        const boost::system::error_code& ec)
{
    if (ec)
    {
        if (ec.value() == EAGAIN)
        {
            messages::serviceTemporarilyUnavailable(response, "5");
        }
        else
        {
            messages::operationFailed(response);
        }
        return;
    }
    // The backend returns only after all 16 readbacks agree.
    messages::success(response);
}

inline void get(const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                const std::optional<std::string>& chassisPath)
{
    withService(
        asyncResp, chassisPath, [asyncResp](const std::string& service) {
            dbus::utility::async_method_call(
                [asyncResp](const boost::system::error_code& ec,
                            const ProfileSnapshot& snapshot) {
                    // object_server returns a tuple as ONE D-Bus
                    // struct, not three separate output arguments.
                    const auto& [mode, profiles, profileStatus] = snapshot;
                    completeGet(asyncResp->res, ec, mode, profiles,
                                profileStatus);
                },
                service, std::string(path), std::string(interface),
                "GetPowerProfile");
        });
}

inline void patch(const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                  const std::optional<std::string>& chassisPath,
                  const std::string& mode)
{
    withService(asyncResp, chassisPath,
                [asyncResp, mode](const std::string& service) {
                    dbus::utility::async_method_call(
                        [asyncResp](const boost::system::error_code& ec) {
                            completeSet(asyncResp->res, ec);
                        },
                        service, std::string(path), std::string(interface),
                        "SetPowerMode", mode);
                });
}
} // namespace redfish::lpu_power_mode
