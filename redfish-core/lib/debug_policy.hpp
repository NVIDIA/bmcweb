#pragma once

#include "bmcweb_config.h"

#include "async_resp.hpp"
#include "dbus_singleton.hpp"
#include "error_messages.hpp"
#include "http_response.hpp"
#include "logging.hpp"

#include <app.hpp>
#include <boost/algorithm/string.hpp>
#include <boost/container/flat_map.hpp>
#include <boost/system/detail/error_code.hpp>
#include <dbus_utility.hpp>
#include <nlohmann/json.hpp>
#include <query.hpp>
#include <registries/privilege_registry.hpp>
#include <sdbusplus/asio/property.hpp>
#include <sdbusplus/unpack_properties.hpp>
#include <utils/conditions_utils.hpp>
#include <utils/dbus_log_utils.hpp>
#include <utils/dbus_utils.hpp>
#include <utils/fw_utils.hpp>
#include <utils/json_utils.hpp>
#include <utils/sw_utils.hpp>

#include <functional>
#include <memory>
#include <vector>

namespace redfish
{
namespace policy::impl
{
using namespace std::string_literals;
const auto remoteDebugIntfc =
    "xyz.openbmc_project.Control.Processor.RemoteDebug"s;

inline std::string debugStateToString(const std::string& dbusStr)
{
    using namespace std::string_literals;
    static const auto debugStatePreffix =
        "xyz.openbmc_project.Control.Processor.RemoteDebug.DebugState."s;
    const auto pos = dbusStr.find(debugStatePreffix);
    if (pos != 0)
    {
        return {};
    }
    return dbusStr.substr(debugStatePreffix.size());
}

} // namespace policy::impl

inline void debugPropertiesFill(crow::Response& resp,
                                const dbus::utility::DBusPropertiesMap& prop)
{
    std::optional<std::string> jtagDebug, deviceDebug,
        securePrivilegeInvasiveDebug, securePrivilegeNonInvasiveDebug,
        nonInvasiveDebug, invasiveDebug;
    std::optional<unsigned> timeout;

    const bool success = sdbusplus::unpackPropertiesNoThrow(
        dbus_utils::UnpackErrorPrinter(), prop, "JtagDebug", jtagDebug,
        "DeviceDebug", deviceDebug, "SecurePrivilegeNonInvasiveDebug",
        securePrivilegeNonInvasiveDebug, "SecurePrivilegeInvasiveDebug",
        securePrivilegeInvasiveDebug, "NonInvasiveDebug", nonInvasiveDebug,
        "InvasiveDebug", invasiveDebug, "Timeout", timeout);
    if (!success)
    {
        messages::internalError(resp);
        return;
    }
    if (jtagDebug)
    {
        resp.jsonValue["Oem"]["Nvidia"]["ProcessorDebugCapabilities"]
                      ["JtagDebug"] =
            policy::impl::debugStateToString(jtagDebug.value());
    }
    if (deviceDebug)
    {
        resp.jsonValue["Oem"]["Nvidia"]["ProcessorDebugCapabilities"]
                      ["DeviceDebug"] =
            policy::impl::debugStateToString(deviceDebug.value());
    }
    if (securePrivilegeNonInvasiveDebug)
    {
        resp.jsonValue["Oem"]["Nvidia"]["ProcessorDebugCapabilities"]
                      ["SecurePrivilegeNonInvasiveDebug"] =
            policy::impl::debugStateToString(
                securePrivilegeNonInvasiveDebug.value());
    }
    if (securePrivilegeInvasiveDebug)
    {
        resp.jsonValue["Oem"]["Nvidia"]["ProcessorDebugCapabilities"]
                      ["SecurePrivilegeInvasiveDebug"] =
            policy::impl::debugStateToString(
                securePrivilegeInvasiveDebug.value());
    }
    if (invasiveDebug)
    {
        resp.jsonValue["Oem"]["Nvidia"]["ProcessorDebugCapabilities"]
                      ["InvasiveDebug"] =
            policy::impl::debugStateToString(invasiveDebug.value());
    }
    if (nonInvasiveDebug)
    {
        resp.jsonValue["Oem"]["Nvidia"]["ProcessorDebugCapabilities"]
                      ["NonInvasiveDebug"] =
            policy::impl::debugStateToString(nonInvasiveDebug.value());
    }
    if (timeout)
    {
        resp.jsonValue["Oem"]["Nvidia"]["ProcessorDebugCapabilities"]
                      ["Timeout"] = timeout.value();
    }
}

inline void
    debugPropertiesGet(const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                       const std::string& svc, const std::string& path)
{
    auto propCallback =
        [asyncResp](const boost::system::error_code ec,
                    const dbus::utility::DBusPropertiesMap& prop) {
            if (ec)
            {
                if (ec == boost::system::errc::host_unreachable)
                {
                    // Service not available, no error, just don't return
                    // chassis state info
                    BMCWEB_LOG_ERROR << "Service not available " << ec;
                    return;
                }
                BMCWEB_LOG_ERROR << "DBUS response error " << ec;
                messages::internalError(asyncResp->res);
                return;
            }
            debugPropertiesFill(asyncResp->res, prop);
        };

    sdbusplus::asio::getAllProperties(*crow::connections::systemBus, svc, path,
                                      policy::impl::remoteDebugIntfc,
                                      propCallback);
}

using findDebugInterfaceCallback =
    std::function<void(std::shared_ptr<bmcweb::AsyncResp>, // Async response
                       const std::string&,                 // Service
                       const std::string&)>;               // Path

inline void
    findDebugInterface(const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                       const findDebugInterfaceCallback& dbgCallback)
{
    auto respHandler =
        [asyncResp,
         dbgCallback](const boost::system::error_code ec,
                      const dbus::utility::MapperGetSubTreeResponse& subtree) {
            if (ec.value() == EBADR)
            {
                messages::resourceNotFound(asyncResp->res, "DebugInterface",
                                           policy::impl::remoteDebugIntfc);
                return;
            }

            if (ec)
            {
                BMCWEB_LOG_ERROR << "DBUS response error " << ec;
                messages::internalError(asyncResp->res);
                return;
            }
            for (const auto& [path, object] : subtree)
            {
                for (const auto& [svc, ifcs] : object)
                {
                    if (std::find(ifcs.begin(), ifcs.end(),
                                  policy::impl::remoteDebugIntfc) != ifcs.end())
                    {
                        dbgCallback(asyncResp, svc, path);
                        return;
                    }
                }
            }
            dbgCallback(asyncResp, "", "");
        };
    crow::connections::systemBus->async_method_call(
        respHandler, "xyz.openbmc_project.ObjectMapper",
        "/xyz/openbmc_project/object_mapper",
        "xyz.openbmc_project.ObjectMapper", "GetSubTree",
        "/xyz/openbmc_project/control", 0,
        std::array<const char*, 1>{
            "xyz.openbmc_project.Control.Processor.RemoteDebug"});
}

inline void
    handleDebugPolicyGet(const std::shared_ptr<bmcweb::AsyncResp>& asyncResp)
{
    auto getPropCallback =
        [](const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
           const std::string& svc, const std::string& path) {
            if (path.empty())
            {
                // Not an error :: partial success
                // propertyMissing:Indicates that a required property was not
                // supplied as part of the request.
                nlohmann::json missingProperty = messages::propertyMissing(
                    "Oem/Nvidia/ProcessorDebugCapabilities");
                messages::moveErrorsToErrorJson(asyncResp->res.jsonValue,
                                                missingProperty);
                return;
            }
            debugPropertiesGet(asyncResp, svc, path);
        };
    findDebugInterface(asyncResp, getPropCallback);
}

inline void debugCapabilitiesProcess(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp, const std::string& svc,
    const std::string& path, const std::string& method,
    const std::vector<std::string>& caps)
{
    crow::connections::systemBus->async_method_call(
        [asyncResp, method, caps](const boost::system::error_code ec) {
            if (ec)
            {
                BMCWEB_LOG_ERROR << "DBUS response error: " << method << " "
                                 << ec;
                messages::internalError(asyncResp->res);
                return;
            }
            messages::success(asyncResp->res, method);
        },
        svc, path, "xyz.openbmc_project.Control.Processor.RemoteDebug", method,
        caps);
}

inline void
    debugPropertySet(const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                     const std::string& svc, const std::string& path,
                     const std::string& prop, unsigned value)
{
    crow::connections::systemBus->async_method_call(
        [asyncResp, prop, value](const boost::system::error_code ec) {
            if (ec)
            {
                BMCWEB_LOG_ERROR << "DBUS response error: Set " << prop << " "
                                 << ec;
                messages::internalError(asyncResp->res);
                return;
            }
            messages::success(asyncResp->res, prop);
        },
        svc, path, "org.freedesktop.DBus.Properties", "Set",
        "xyz.openbmc_project.Control.Processor.RemoteDebug", prop,
        dbus::utility::DbusVariantType(value));
}

inline bool fetchDebugPropertyFromJson(nlohmann::json& json,
                                       std::string_view prop,
                                       std::optional<bool>& val)
{
    using namespace std::string_literals;
    std::string propStr;
    if (!redfish::json_util::getValueFromJsonObject(json, std::string(prop),
                                                    propStr))
    {
        return true;
    }
    if (propStr == "Enable"s)
    {
        val = true;
        return true;
    }
    if (propStr == "Disable"s)
    {
        val = false;
        return true;
    }
    return false;
}

inline bool fetchDebugTimeoutPropertyFromJson(nlohmann::json& json,
                                              std::optional<unsigned>& val)
{
    const auto key = "Timeout";
    auto it = json.find(key);
    if (it == json.end())
    {
        return true;
    }
    if (!json[key].is_number_unsigned())
    {
        BMCWEB_LOG_ERROR << "Key Timeout is not a number";
        return false;
    }
    val = json[key].get<unsigned>();
    return true;
}

inline void handleDebugPolicyPatchReq(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    nlohmann::json& procCap)
{
    using namespace std::string_literals;
    const std::vector<std::string> caps{"DeviceDebug",
                                        "InvasiveDebug",
                                        "JtagDebug",
                                        "NonInvasiveDebug",
                                        "SecurePrivilegeInvasiveDebug",
                                        "SecurePrivilegeNonInvasiveDebug"};
    std::optional<unsigned> timeout;
    std::vector<std::string> capsToEnable;
    std::vector<std::string> capsToDisable;

    for (const auto& cap : caps)
    {
        std::optional<bool> value;
        if (!fetchDebugPropertyFromJson(procCap, cap, value))
        {
            BMCWEB_LOG_ERROR << cap << " property error";
            messages::propertyUnknown(asyncResp->res, cap);
            return;
        }
        if (!value)
        {
            continue;
        }
        if (*value)
        {
            capsToEnable.push_back(
                "xyz.openbmc_project.Control.Processor.RemoteDebug.DebugPolicy."s +
                cap);
            continue;
        }
        capsToDisable.push_back(
            "xyz.openbmc_project.Control.Processor.RemoteDebug.DebugPolicy."s +
            cap);
    }
    if (!fetchDebugTimeoutPropertyFromJson(procCap, timeout))
    {
        BMCWEB_LOG_ERROR << "Timeout property error";
        messages::propertyUnknown(asyncResp->res, "Timeout");
        return;
    }
    auto propSetCallback =
        [capsToEnable, capsToDisable,
         timeout](const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                  const std::string& svc, const std::string& path) {
            if (path.empty())
            {
                messages::internalError(asyncResp->res);
                return;
            }
            if (!capsToEnable.empty())
            {
                debugCapabilitiesProcess(asyncResp, svc, path, "Enable"s,
                                         capsToEnable);
            }
            if (!capsToDisable.empty())
            {
                debugCapabilitiesProcess(asyncResp, svc, path, "Disable"s,
                                         capsToDisable);
            }
            if (timeout)
            {
                debugPropertySet(asyncResp, svc, path, "Timeout", *timeout);
            }
        };

    findDebugInterface(asyncResp, propSetCallback);
}

} // namespace redfish
