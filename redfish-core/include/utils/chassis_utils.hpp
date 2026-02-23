// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: Copyright OpenBMC Authors
#pragma once
#include "async_resp.hpp"
#include "background_copy.hpp"
#include "boost_formatters.hpp"
#include "dbus_utility.hpp"
#include "error_messages.hpp"
#include "in_band.hpp"
#include "logging.hpp"
#include "nvidia_async_call_utils.hpp"
#include "nvidia_dbus_utility.hpp"
#include "utils/dbus_utils.hpp"

#include <boost/container/flat_map.hpp>
#include <sdbusplus/message/native_types.hpp>

#include <array>
#include <fstream>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace redfish
{
static constexpr std::array<std::string_view, 2> chassisInterfaces = {
    "xyz.openbmc_project.Inventory.Item.Board",
    "xyz.openbmc_project.Inventory.Item.Chassis"};

namespace chassis_utils
{
constexpr const char* acceleratorInvIntf =
    "xyz.openbmc_project.Inventory.Item.Accelerator";

constexpr const char* cpuInvIntf = "xyz.openbmc_project.Inventory.Item.Cpu";

constexpr const char* nvLinkMgmtInvIntf =
    "xyz.openbmc_project.Inventory.Item.NetworkInterface";

constexpr const char* nvSwitchInvIntf =
    "xyz.openbmc_project.Inventory.Item.NvSwitch";

constexpr const char* bmcInvInterf = "xyz.openbmc_project.Inventory.Item.BMC";

constexpr const char* chassisInvInterf =
    "xyz.openbmc_project.Inventory.Item.Chassis";

constexpr const char* gpmMetricsIntf = "com.nvidia.GPMMetrics";

using Associations =
    std::vector<std::tuple<std::string, std::string, std::string>>;

using GetObjectType =
    std::vector<std::pair<std::string, std::vector<std::string>>>;

inline std::string getPowerStateType(const std::string& stateType)
{
    if (stateType == "xyz.openbmc_project.State.Decorator.OperationalStatus."
                     "StateType.Absent")
    {
        return "Absent";
    }
    if (stateType == "xyz.openbmc_project.State.Decorator.OperationalStatus."
                     "StateType.Deferring")
    {
        return "Deferring";
    }
    if (stateType == "xyz.openbmc_project.State.Decorator.OperationalStatus."
                     "StateType.Disabled")
    {
        return "Disabled";
    }
    if (stateType == "xyz.openbmc_project.State.Decorator.OperationalStatus."
                     "StateType.Enabled")
    {
        return "Enabled";
    }
    if (stateType == "xyz.openbmc_project.State.Decorator.OperationalStatus."
                     "StateType.StandbyOffline")
    {
        return "StandbyOffline";
    }
    if (stateType == "xyz.openbmc_project.State.Decorator.OperationalStatus."
                     "StateType.Starting")
    {
        return "Starting";
    }
    if (stateType == "xyz.openbmc_project.State.Decorator.OperationalStatus."
                     "StateType.UnavailableOffline")
    {
        return "UnavailableOffline";
    }
    if (stateType == "xyz.openbmc_project.State.Decorator.OperationalStatus."
                     "StateType.Updating")
    {
        return "Updating";
    }
    // Unknown or others
    return "";
}

inline void resetPowerLimit(const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                            const std::string& path,
                            const std::string& connection)
{
    static const std::string clearPowerCapAsyncIntf{
        "com.nvidia.Common.ClearPowerCapAsync"};

    dbus::utility::getDbusObject(
        path, std::array<std::string_view, 1>{clearPowerCapAsyncIntf},
        [asyncResp, path,
         connection](const boost::system::error_code& ec,
                     const dbus::utility::MapperGetObject& object) {
            if (!ec && !object.empty())
            {
                const auto& [serv, _] = object[0];
                BMCWEB_LOG_DEBUG("Performing Post using Async Method Call");

                nvidia_async_operation_utils::doGenericCallAsyncAndGatherResult<
                    int32_t>(
                    asyncResp, std::chrono::seconds(60), serv, path,
                    clearPowerCapAsyncIntf, "ClearPowerCap",
                    [asyncResp](const std::string& status,
                                [[maybe_unused]] const int32_t* retValue) {
                        if (status == nvidia_async_operation_utils::
                                          asyncStatusValueSuccess)
                        {
                            BMCWEB_LOG_DEBUG("PowerLimit Reset Succeeded");
                            messages::success(asyncResp->res);
                            return;
                        }
                        BMCWEB_LOG_ERROR("resetPowerLimit error {}", status);
                        messages::internalError(asyncResp->res);
                    });
                return;
            }

            BMCWEB_LOG_DEBUG("Performing Post using Sync Method Call");

            dbus::utility::async_method_call(
                [asyncResp](boost::system::error_code& ec1,
                            const int retValue) {
                    if (!ec1)
                    {
                        if (retValue != 0)
                        {
                            BMCWEB_LOG_ERROR("resetPowerLimit error {}",
                                             retValue);
                            messages::internalError(asyncResp->res);
                        }
                        BMCWEB_LOG_DEBUG("PowerLimit Reset Succeeded");
                        messages::success(asyncResp->res);
                        return;
                    }
                    BMCWEB_LOG_ERROR("PowerLimit Reset error {}", ec1);
                    messages::internalError(asyncResp->res);
                    return;
                },
                connection, path, "com.nvidia.Common.ClearPowerCap",
                "ClearPowerCap");
        });
}

inline std::string getFeatureReadyStateType(const std::string& stateType)
{
    if (stateType == "xyz.openbmc_project.State.FeatureReady.States.Enabled")
    {
        return "Enabled";
    }
    if (stateType ==
        "xyz.openbmc_project.State.FeatureReady.States.StandbyOffline")
    {
        return "StandbyOffline";
    }
    if (stateType == "xyz.openbmc_project.State.FeatureReady.States.Starting")
    {
        return "Starting";
    }
    if (stateType == "xyz.openbmc_project.State.FeatureReady.States.Disabled")
    {
        return "Disabled";
    }
    if (stateType == "xyz.openbmc_project.State.FeatureReady.States.Unknown")
    {
        return "Unknown";
    }
    // Unknown or others
    return "";
}

/**
 * @brief Convert state of EstimatePowerMethod PDI
 * @param state   stateOfEstimatePowerMEthod property of static power hint
 * PDI
 */
inline std::string getStateOfEstimatePowerMethod(const std::string& state)
{
    if (state == "com.nvidia.StaticPowerHint.StateOfEstimatePower.Completed")
    {
        return "Completed";
    }
    if (state == "com.nvidia.StaticPowerHint.StateOfEstimatePower.InProgress")
    {
        return "InProgress";
    }
    if (state == "com.nvidia.StaticPowerHint.StateOfEstimatePower.Failed")
    {
        return "Failed";
    }
    if (state ==
        "com.nvidia.StaticPowerHint.StateOfEstimatePower.InvalidArgument")
    {
        return "InvalidArgument";
    }
    if (state == "com.nvidia.StaticPowerHint.StateOfEstimatePower.Invalid")
    {
        return "Invalid";
    }
    // Unknown or others
    return "";
}

/**
 * @brief Retrieves valid chassis ID
 * @param asyncResp   Pointer to object holding response data
 * @param callback  Callback for next step to get valid chassis ID
 */
template <typename Callback>
void getValidChassisID(const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                       const std::string& chassisID, Callback&& callback)
{
    BMCWEB_LOG_DEBUG("checkChassisId enter");
    const std::array<std::string_view, 2> interfaces = {
        "xyz.openbmc_project.Inventory.Item.Board",
        "xyz.openbmc_project.Inventory.Item.Chassis"};

    auto respHandler = [callback{std::forward<Callback>(callback)}, asyncResp,
                        chassisID](
                           const boost::system::error_code& ec,
                           const std::vector<std::string>& chassisPaths) {
        BMCWEB_LOG_DEBUG("getValidChassisID respHandler enter");
        if (ec)
        {
            BMCWEB_LOG_ERROR("getValidChassisID respHandler DBUS error: {}",
                             ec);
            messages::internalError(asyncResp->res);
            return;
        }

        std::optional<std::string> validChassisID;
        std::string chassisName;
        for (const std::string& chassis : chassisPaths)
        {
            sdbusplus::message::object_path path(chassis);
            chassisName = path.filename();
            if (chassisName.empty())
            {
                BMCWEB_LOG_ERROR("Failed to find chassisName in {}", chassis);
                continue;
            }
            if (chassisName == chassisID)
            {
                validChassisID = chassisID;
                break;
            }
        }
        callback(validChassisID);
    };

    // Get the Chassis Collection
    dbus::utility::getSubTreePaths("/xyz/openbmc_project/inventory", 0,
                                   interfaces, respHandler);
    BMCWEB_LOG_DEBUG("checkChassisId exit");
}

/**
 * @brief Retrieves valid chassis path
 * @param asyncResp   Pointer to object holding response data
 * @param callback  Callback for next step to get valid chassis path
 */
template <typename Callback>
void getValidChassisPath(const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                         const std::string& chassisId, Callback&& callback)
{
    BMCWEB_LOG_DEBUG("checkChassisId enter");

    // Get the Chassis Collection
    dbus::utility::getSubTreePaths(
        "/xyz/openbmc_project/inventory", 0, chassisInterfaces,
        [callback = std::forward<Callback>(callback), asyncResp,
         chassisId](const boost::system::error_code& ec,
                    const dbus::utility::MapperGetSubTreePathsResponse&
                        chassisPaths) mutable {
            BMCWEB_LOG_DEBUG("getValidChassisPath respHandler enter");
            if (ec)
            {
                BMCWEB_LOG_ERROR(
                    "getValidChassisPath respHandler DBUS error: {}", ec);
                messages::internalError(asyncResp->res);
                return;
            }

            std::optional<std::string> chassisPath;
            for (const std::string& chassis : chassisPaths)
            {
                sdbusplus::message::object_path path(chassis);
                std::string chassisName = path.filename();
                if (chassisName.empty())
                {
                    BMCWEB_LOG_ERROR("Failed to find '/' in {}", chassis);
                    continue;
                }
                if (chassisName == chassisId)
                {
                    chassisPath = chassis;
                    break;
                }
            }
            callback(chassisPath);
        });
    BMCWEB_LOG_DEBUG("checkChassisId exit");
}

/**
 * @brief Retrieves valid chassis path and interfaces
 * @param asyncResp   Pointer to object holding response data
 * @param callback  Callback for next step to get valid chassis path
 */
template <typename Callback>
void getValidChassisPathAndInterfaces(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& chassisId, Callback&& callback)
{
    BMCWEB_LOG_DEBUG("check ChassisPathAndInterfaces enter");
    constexpr std::array<std::string_view, 2> interfaces = {
        "xyz.openbmc_project.Inventory.Item.Board",
        "xyz.openbmc_project.Inventory.Item.Chassis"};

    dbus::utility::getSubTree(
        "/xyz/openbmc_project/inventory", 0, interfaces,
        [callback = std::forward<Callback>(callback), asyncResp, chassisId](
            const boost::system::error_code& ec,
            const dbus::utility::MapperGetSubTreeResponse& subtree) mutable {
            BMCWEB_LOG_DEBUG(
                "getValidChassisPathAndInterfaces respHandler enter");
            if (ec || subtree.empty())
            {
                BMCWEB_LOG_ERROR(
                    "getValidChassisPathAndInterfaces respHandler DBUS error: {}",
                    ec);
                messages::internalError(asyncResp->res);
                return;
            }

            std::optional<std::string> chassisPath;
            std::vector<std::string> interfacesOnChassisPath;
            for (const std::pair<std::string,
                                 std::vector<std::pair<
                                     std::string, std::vector<std::string>>>>&
                     object : subtree)
            {
                const std::string& chassis = object.first;
                const std::vector<
                    std::pair<std::string, std::vector<std::string>>>&
                    connectionNames = object.second;

                sdbusplus::message::object_path path(chassis);
                std::string chassisName = path.filename();
                if (chassisName.empty())
                {
                    BMCWEB_LOG_ERROR("Failed to find '/' in {}", chassis);
                    continue;
                }
                if (chassisName == chassisId)
                {
                    chassisPath = chassis;
                    interfacesOnChassisPath = connectionNames[0].second;
                    break;
                }
            }
            callback(interfacesOnChassisPath, chassisPath);
        });
    BMCWEB_LOG_DEBUG("check ChassisPathAndInterfaces exit");
}

/**
 * @brief Fill out links association to parent chassis by
 * requesting data from the given D-Bus association object.
 *
 * @param[in,out]   aResp       Async HTTP response.
 * @param[in]       objPath     D-Bus object to query.
 */
inline void getChassisLinksContainedBy(
    const std::shared_ptr<bmcweb::AsyncResp>& aResp, const std::string& objPath)
{
    BMCWEB_LOG_DEBUG("Get parent chassis link");
    dbus::utility::getProperty<std::vector<std::string>>(
        "xyz.openbmc_project.ObjectMapper", objPath + "/parent_chassis",
        "xyz.openbmc_project.Association", "endpoints",
        [aResp](const boost::system::error_code& ec2,
                const std::vector<std::string>& data) {
            if (ec2)
            {
                return; // no chassis = no failures
            }

            if (data.size() > 1)
            {
                // There must be single parent chassis
                return;
            }
            const std::string& chassisPath = data.front();
            sdbusplus::message::object_path objectPath(chassisPath);
            std::string chassisName = objectPath.filename();
            if (chassisName.empty())
            {
                messages::internalError(aResp->res);
                return;
            }
            aResp->res.jsonValue["Links"]["ContainedBy"] = {
                {"@odata.id", "/redfish/v1/Chassis/" + chassisName}};
        });
}

inline void getChassisLocationType(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& connectionName, const std::string& path)
{
    dbus::utility::getProperty<std::string>(
        connectionName, path,
        "xyz.openbmc_project.Inventory.Decorator.Location", "LocationType",
        [asyncResp](const boost::system::error_code& ec,
                    const std::string& property) {
            if (ec)
            {
                BMCWEB_LOG_DEBUG("DBUS response error for Location");
                messages::internalError(asyncResp->res);
                return;
            }

            asyncResp->res
                .jsonValue["Location"]["PartLocation"]["LocationType"] =
                redfish::dbus_utils::toLocationType(property);
        });
}

inline void getChassisLocationCode(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& connectionName, const std::string& path)
{
    dbus::utility::getProperty<std::string>(
        connectionName, path,
        "xyz.openbmc_project.Inventory.Decorator.LocationCode", "LocationCode",
        [asyncResp](const boost::system::error_code& ec,
                    const std::string& property) {
            if (ec)
            {
                BMCWEB_LOG_ERROR("DBUS response error for LocationCode");
                messages::internalError(asyncResp->res);
                return;
            }

            asyncResp->res
                .jsonValue["Location"]["PartLocation"]["ServiceLabel"] =
                property;
        });
}

inline void getChassisLocationContext(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& connectionName, const std::string& path)
{
    dbus::utility::getProperty<std::string>(
        connectionName, path,
        "xyz.openbmc_project.Inventory.Decorator.LocationContext",
        "LocationContext",
        [asyncResp](const boost::system::error_code& ec,
                    const std::string& property) {
            if (ec)
            {
                BMCWEB_LOG_ERROR("DBUS response error for LocationContext");
                messages::internalError(asyncResp->res);
                return;
            }

            asyncResp->res.jsonValue["Location"]["PartLocationContext"] =
                property;
        });
}

inline void getChassisReplaceable(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& connectionName, const std::string& path)
{
    dbus::utility::getProperty<bool>(
        connectionName, path,
        "xyz.openbmc_project.Inventory.Decorator.Replaceable",
        "FieldReplaceable",
        [asyncResp](const boost::system::error_code& ec, const bool& property) {
            if (ec)
            {
                BMCWEB_LOG_ERROR("DBUS response error for Replaceable");
                // not return Internal Error because it can be an optional
                // property
                return;
            }

            asyncResp->res.jsonValue["Replaceable"] = property;
        });
}

/**
 * @brief Translates PowerMode DBUS property value to redfish.
 *
 * @param[in] dbusAction    The powerMode action in D-BUS.
 *
 * @return Returns as a string, the powermode in Redfish terms. If
 * translation cannot be done, returns an empty string.
 */
inline std::string getPowerModeType(const std::string& dbusAction)
{
    if (dbusAction ==
        "xyz.openbmc_project.Control.Power.Mode.PowerMode.MaximumPerformance")
    {
        return "Custom";
    }
    if (dbusAction ==
        "xyz.openbmc_project.Control.Power.Mode.PowerMode.PowerSaving")
    {
        return "MaxQ";
    }
    if (dbusAction == "xyz.openbmc_project.Control.Power.Mode.PowerMode.OEM")
    {
        return "Custom";
    }

    return "";
}

template <typename CallbackFunc>
inline void getAssociationEndpoint(const std::string& objPath,
                                   CallbackFunc&& callback)
{
    dbus::utility::getProperty<std::vector<std::string>>(
        "xyz.openbmc_project.ObjectMapper", objPath,
        "xyz.openbmc_project.Association", "endpoints",
        [callback, objPath](const boost::system::error_code& ec,
                            const std::vector<std::string>& resp) {
            if (ec)
            {
                BMCWEB_LOG_ERROR(
                    "D-Bus responses error: {} (busctl call {} {} {} Get ss {} endpoints)",
                    ec, dbus_utils::mapperBusName, objPath,
                    dbus_utils::propertyInterface,
                    dbus_utils::associationInterface);
                callback(false, std::string(""));
                return; // should have associated inventory object.
            }

            if (resp.empty())
            {
                BMCWEB_LOG_ERROR(
                    "Data is empty (busctl call {} {} {} Get ss {} endpoints)",
                    dbus_utils::mapperBusName, objPath,
                    dbus_utils::propertyInterface,
                    dbus_utils::associationInterface);
                /*
                                Object must have associated inventory
                   object. Exemplary test on hardware: busctl call
                   xyz.openbmc_project.ObjectMapper
                   \
                                    /xyz/openbmc_project/inventory/system/chassis/HGX_ERoT_FPGA_0/inventory
                   \
                                    org.freedesktop.DBus.Properties \
                                    Get ss xyz.openbmc_project.Association
                   endpoints Response: v as 1
                   "/xyz/openbmc_project/inventory/system/processors/FPGA_0"
                */
                callback(false, std::string(""));
                return;
            }
            // Getting only the first endpoint as we have 1*1 relationship
            // with ERoT and the inventory backed by it.
            const std::string& endpointPath = resp.front();
            callback(true, endpointPath);
        });
}

template <typename CallbackFunc>
inline void getRedfishURL(const std::filesystem::path& invObjPath,
                          CallbackFunc&& callback)
{
    BMCWEB_LOG_DEBUG("getRedfishURL({})", invObjPath.string());
    dbus::utility::getDbusObject(
        invObjPath, std::array<std::string_view, 0>{},
        [callback, invObjPath](const boost::system::error_code& ec,
                               const dbus::utility::MapperGetObject& resp) {
            std::string urlStr;
            if (ec || resp.empty())
            {
                return;
            }
            // if accelerator interface then the object would be
            // of type fpga or GPU.
            // If switch interface then it could be Nvswitch or
            // PcieSwitch else it is BMC
            for (const auto& serObj : resp)
            {
                std::string service = serObj.first;
                auto interfaces = serObj.second;

                for (const auto& interface : interfaces)
                {
                    if (interface == acceleratorInvIntf ||
                        interface == cpuInvIntf || interface == gpmMetricsIntf)
                    {
                        /*
                        busctl call xyz.openbmc_project.ObjectMapper
                        /xyz/openbmc_project/object_mapper
                        xyz.openbmc_project.ObjectMapper GetObject sas
                        /xyz/openbmc_project/inventory/system/chassis/HGX_GPU_SXM_1
                        0 a{sas} 2
                         - "xyz.openbmc_project.GpuMgr" ...
                         - "xyz.openbmc_project.ObjectMapper" ...
                        busctl call xyz.openbmc_project.ObjectMapper
                        /xyz/openbmc_project/object_mapper
                        xyz.openbmc_project.ObjectMapper GetObject sas
                        /xyz/openbmc_project/inventory/system/chassis/HGX_FPGA_0
                        0 a{sas} 2
                           - "xyz.openbmc_project.GpuMgr" ...
                           - "xyz.openbmc_project.ObjectMapper" ...
                        */
                        urlStr =
                            std::string(
                                "/redfish/v1/Systems/" +
                                std::string(BMCWEB_REDFISH_SYSTEM_URI_NAME) +
                                "/Processors/") +
                            invObjPath.filename().string();
                        BMCWEB_LOG_DEBUG("{} {} => URL: {}", service, interface,
                                         urlStr);
                        callback(true, urlStr);
                        return;
                    }
                    if (interface == chassisInvInterf)
                    {
                        urlStr = std::string("/redfish/v1/Chassis/") +
                                 invObjPath.filename().string();
                        BMCWEB_LOG_DEBUG("{} {} => URL: {}", service, interface,
                                         urlStr);
                        callback(true, urlStr);
                        return;
                    }
                    if (interface == nvLinkMgmtInvIntf)
                    {
                        const std::string chassisPrefixDbus =
                            "/xyz/openbmc_project/inventory/system/chassis/";
                        if (invObjPath.string().find(chassisPrefixDbus) !=
                            std::string::npos)
                        {
                            std::string url =
                                std::string("/redfish/v1/Chassis/");
                            url += invObjPath.string().substr(
                                chassisPrefixDbus.size());
                            callback(true, url);
                            return;
                        }
                    }
                    if (interface == nvSwitchInvIntf)
                    {
                        /* busctl call xyz.openbmc_project.ObjectMapper
                        /xyz/openbmc_project/object_mapper
                        xyz.openbmc_project.ObjectMapper GetObject sas
                        /xyz/openbmc_project/inventory/system/chassis/HGX_NVSwitch_0
                        0 a{sas} 2
                         - "xyz.openbmc_project.GpuMgr" ...
                         - "xyz.openbmc_project.ObjectMapper" ...
                        */
                        // This is NVSwitch or PCIeSwitch
                        std::string switchID = invObjPath.filename();
                        // Now get the fabric ID
                        BMCWEB_LOG_DEBUG(
                            "DBUS resp: {} {} => getAssociationEndpoint({}/fabrics, CALLBACK)",
                            service, interface, invObjPath.string());
                        getAssociationEndpoint(
                            invObjPath.string() + "/fabrics",
                            [switchID, callback](const bool& status,
                                                 const std::string& ep) {
                                std::string urlResult;
                                if (!status)
                                {
                                    BMCWEB_LOG_DEBUG(
                                        "Unable to get the association endpoint");

                                    callback(false, urlResult);
                                    return;
                                }
                                sdbusplus::message::object_path invObjectPath(
                                    ep);
                                const std::string& fabricID =
                                    invObjectPath.filename();

                                urlResult = std::string("/redfish/v1/Fabrics/");
                                urlResult += fabricID;
                                urlResult += "/Switches/";
                                urlResult += switchID;

                                callback(true, urlResult);
                                return;
                            });
                        return;
                    }
                    if (interface == bmcInvInterf)
                    {
                        urlStr = std::string(
                            "/redfish/v1/Managers/" +
                            std::string(BMCWEB_REDFISH_MANAGER_URI_NAME));
                        BMCWEB_LOG_DEBUG("{} {} => URL: {}", service, interface,
                                         urlStr);
                        callback(true, urlStr);
                        return;
                    }
                }
                BMCWEB_LOG_DEBUG("Not found proper interface for service {}",
                                 service);
            }
            BMCWEB_LOG_ERROR("Failed to find proper URL");
            callback(false, urlStr);
        });
}

} // namespace chassis_utils
} // namespace redfish
