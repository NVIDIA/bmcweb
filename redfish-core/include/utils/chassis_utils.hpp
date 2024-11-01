#pragma once
#include "async_resp.hpp"
#include "background_copy.hpp"
#include "dbus_utility.hpp"
#include "error_messages.hpp"
#include "in_band.hpp"
#include "nvidia_async_call_utils.hpp"

#include <async_resp.hpp>
#include <sdbusplus/asio/connection.hpp>
#include <sdbusplus/asio/property.hpp>
#include <utils/dbus_utils.hpp>

#include <array>
#include <string_view>
namespace redfish
{

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

constexpr const char* bootStatusIntf = "com.nvidia.RoT.BootStatus";

constexpr const char* gpmMetricsIntf = "com.nvidia.GPMMetrics";

using Associations =
    std::vector<std::tuple<std::string, std::string, std::string>>;

using GetSubTreeType = std::vector<
    std::pair<std::string,
              std::vector<std::pair<std::string, std::vector<std::string>>>>>;
using GetObjectType =
    std::vector<std::pair<std::string, std::vector<std::string>>>;

static constexpr uint8_t mctpTypeVDMIANA = 0x7f;

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
        if (!ec)
        {
            for (const auto& [serv, _] : object)
            {
                BMCWEB_LOG_DEBUG("Performing Post using Async Method Call");

                nvidia_async_operation_utils::doGenericCallAsyncAndGatherResult<
                    int>(asyncResp, std::chrono::seconds(60), serv, path,
                         clearPowerCapAsyncIntf, "ClearPowerCap",
                         [asyncResp](const std::string& status,
                                     [[maybe_unused]] const int* retValue) {
                    if (status ==
                        nvidia_async_operation_utils::asyncStatusValueSuccess)
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
        }

        BMCWEB_LOG_DEBUG("Performing Post using Sync Method Call");

        crow::connections::systemBus->async_method_call(
            [asyncResp](boost::system::error_code ec1, const int retValue) {
            if (!ec1)
            {
                if (retValue != 0)
                {
                    BMCWEB_LOG_ERROR("resetPowerLimit error {}", retValue);
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
 * @param state   stateOfEstimatePowerMEthod property of static power hint PDI
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
    const std::array<const char*, 2> interfaces = {
        "xyz.openbmc_project.Inventory.Item.Board",
        "xyz.openbmc_project.Inventory.Item.Chassis"};

    auto respHandler =
        [callback{std::forward<Callback>(callback)}, asyncResp,
         chassisID](const boost::system::error_code ec,
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
    crow::connections::systemBus->async_method_call(
        respHandler, "xyz.openbmc_project.ObjectMapper",
        "/xyz/openbmc_project/object_mapper",
        "xyz.openbmc_project.ObjectMapper", "GetSubTreePaths",
        "/xyz/openbmc_project/inventory", 0, interfaces);
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
    constexpr std::array<std::string_view, 2> interfaces = {
        "xyz.openbmc_project.Inventory.Item.Board",
        "xyz.openbmc_project.Inventory.Item.Chassis"};

    // Get the Chassis Collection
    dbus::utility::getSubTreePaths(
        "/xyz/openbmc_project/inventory", 0, interfaces,
        [callback = std::forward<Callback>(callback), asyncResp,
         chassisId](const boost::system::error_code& ec,
                    const dbus::utility::MapperGetSubTreePathsResponse&
                        chassisPaths) mutable {
        BMCWEB_LOG_DEBUG("getValidChassisPath respHandler enter");
        if (ec)
        {
            BMCWEB_LOG_ERROR("getValidChassisPath respHandler DBUS error: {}",
                             ec);
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
        BMCWEB_LOG_DEBUG("getValidChassisPathAndInterfaces respHandler enter");
        if (ec)
        {
            BMCWEB_LOG_ERROR(
                "getValidChassisPathAndInterfaces respHandler DBUS error: {}",
                ec);
            messages::internalError(asyncResp->res);
            return;
        }

        std::optional<std::string> chassisPath;
        std::vector<std::string> interfacesOnChassisPath;
        for (const std::pair<
                 std::string,
                 std::vector<std::pair<std::string, std::vector<std::string>>>>&
                 object : subtree)
        {
            const std::string& chassis = object.first;
            const std::vector<std::pair<std::string, std::vector<std::string>>>&
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

inline std::string getChassisType(const std::string& chassisType)
{
    if (chassisType ==
        "xyz.openbmc_project.Inventory.Item.Chassis.ChassisType.Component")
    {
        return "Component";
    }
    if (chassisType ==
        "xyz.openbmc_project.Inventory.Item.Chassis.ChassisType.Enclosure")
    {
        return "Enclosure";
    }
    if (chassisType ==
        "xyz.openbmc_project.Inventory.Item.Chassis.ChassisType.Module")
    {
        return "Module";
    }
    if (chassisType ==
        "xyz.openbmc_project.Inventory.Item.Chassis.ChassisType.RackMount")
    {
        return "RackMount";
    }
    if (chassisType ==
        "xyz.openbmc_project.Inventory.Item.Chassis.ChassisType.Shelf")
    {
        return "Shelf";
    }
    if (chassisType ==
        "xyz.openbmc_project.Inventory.Item.Chassis.ChassisType.StandAlone")
    {
        return "StandAlone";
    }
    if (chassisType ==
        "xyz.openbmc_project.Inventory.Item.Chassis.ChassisType.Card")
    {
        return "Card";
    }
    if (chassisType ==
        "xyz.openbmc_project.Inventory.Item.Chassis.ChassisType.Zone")
    {
        return "Zone";
    }
    // Unknown or others
    return "";
}

/**
 * @brief Fill out links association to parent chassis by
 * requesting data from the given D-Bus association object.
 *
 * @param[in,out]   aResp       Async HTTP response.
 * @param[in]       objPath     D-Bus object to query.
 */
inline void
    getChassisLinksContainedBy(const std::shared_ptr<bmcweb::AsyncResp>& aResp,
                               const std::string& objPath)
{
    BMCWEB_LOG_DEBUG("Get parent chassis link");
    crow::connections::systemBus->async_method_call(
        [aResp](const boost::system::error_code ec2,
                std::variant<std::vector<std::string>>& resp) {
        if (ec2)
        {
            return; // no chassis = no failures
        }
        std::vector<std::string>* data =
            std::get_if<std::vector<std::string>>(&resp);
        if (data == nullptr || data->size() > 1)
        {
            // There must be single parent chassis
            return;
        }
        const std::string& chassisPath = data->front();
        sdbusplus::message::object_path objectPath(chassisPath);
        std::string chassisName = objectPath.filename();
        if (chassisName.empty())
        {
            messages::internalError(aResp->res);
            return;
        }
        aResp->res.jsonValue["Links"]["ContainedBy"] = {
            {"@odata.id", "/redfish/v1/Chassis/" + chassisName}};
    },
        "xyz.openbmc_project.ObjectMapper", objPath + "/parent_chassis",
        "org.freedesktop.DBus.Properties", "Get",
        "xyz.openbmc_project.Association", "endpoints");
}

inline void
    getChassisLocationType(const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                           const std::string& connectionName,
                           const std::string& path)
{
    sdbusplus::asio::getProperty<std::string>(
        *crow::connections::systemBus, connectionName, path,
        "xyz.openbmc_project.Inventory.Decorator.Location", "LocationType",
        [asyncResp](const boost::system::error_code ec,
                    const std::string& property) {
        if (ec)
        {
            BMCWEB_LOG_DEBUG("DBUS response error for Location");
            messages::internalError(asyncResp->res);
            return;
        }

        asyncResp->res.jsonValue["Location"]["PartLocation"]["LocationType"] =
            redfish::dbus_utils::toLocationType(property);
    });
}

inline void
    getChassisLocationCode(const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                           const std::string& connectionName,
                           const std::string& path)
{
    sdbusplus::asio::getProperty<std::string>(
        *crow::connections::systemBus, connectionName, path,
        "xyz.openbmc_project.Inventory.Decorator.LocationCode", "LocationCode",
        [asyncResp](const boost::system::error_code& ec,
                    const std::string& property) {
        if (ec)
        {
            BMCWEB_LOG_ERROR("DBUS response error for LocationCode");
            messages::internalError(asyncResp->res);
            return;
        }

        asyncResp->res.jsonValue["Location"]["PartLocation"]["ServiceLabel"] =
            property;
    });
}

inline void getChassisLocationContext(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& connectionName, const std::string& path)
{
    sdbusplus::asio::getProperty<std::string>(
        *crow::connections::systemBus, connectionName, path,
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

        asyncResp->res.jsonValue["Location"]["PartLocationContext"] = property;
    });
}

inline void
    getChassisReplaceable(const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                          const std::string& connectionName,
                          const std::string& path)
{
    sdbusplus::asio::getProperty<bool>(
        *crow::connections::systemBus, connectionName, path,
        "xyz.openbmc_project.Inventory.Decorator.Replaceable",
        "FieldReplaceable",
        [asyncResp](const boost::system::error_code& ec, const bool property) {
        if (ec)
        {
            BMCWEB_LOG_ERROR("DBUS response error for Replaceable");
            // not return Internal Error because it can be an optional property
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
        return "MaxP";
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

enum class InBandOption
{
    BackgroundCopyStatus,
    setBackgroundCopyEnabled,
    setInBandEnabled,
};

/**
 *@brief handles all calls to ERoT mctp UUID for
 *      setBackgroundCopyEnabled, setInBandEnabled,
 *      and getBackgroundCopyAndInBandInfo
 *
 * @param req   Pointer to object holding request data
 * @param asyncResp   Pointer to object holding response data
 * @param chassisUUID  Chassis UUID
 * @param option Determines which method will be called
 * @param enabled Enable or disable the background copy
 * @param chassisID  Chassis ID

 *
 * @return None.
 */

inline void
    handleMctpInBandActions(const crow::Request& req,
                            const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                            const std::string& chassisUUID,
                            const InBandOption option, bool enabled = false,
                            const std::string& chassisId = "")
{
    constexpr std::array<std::string_view, 1> interfaces = {
        "org.freedesktop.DBus.ObjectManager"};

    dbus::utility::getDbusObject(
        "/xyz/openbmc_project/mctp", interfaces,
        [req, asyncResp, chassisId, chassisUUID, option,
         enabled](const boost::system::error_code& ec,
                  const dbus::utility::MapperGetObject& resp) mutable {
        if (ec || resp.empty())
        {
            BMCWEB_LOG_WARNING(
                "DBUS response error during getting of service name: {}", ec);
            return;
        }
        for (auto it = resp.begin(); it != resp.end(); ++it)
        {
            std::string serviceName = it->first;
            crow::connections::systemBus->async_method_call(
                [req, asyncResp, chassisUUID, serviceName, option, enabled,
                 chassisId](const boost::system::error_code& ec,
                            const dbus::utility::ManagedObjectType& resp) {
                if (ec)
                {
                    BMCWEB_LOG_DEBUG("DBUS response error for MCTP.Control");
                    messages::internalError(asyncResp->res);
                    return;
                }

                const uint32_t* eid = nullptr;
                const std::string* uuid = nullptr;
                const std::vector<uint8_t>* supportedMsgTypes = nullptr;
                bool foundEID = false;

                for (auto& objectPath : resp)
                {
                    for (auto& interfaceMap : objectPath.second)
                    {
                        if (interfaceMap.first ==
                            "xyz.openbmc_project.Common.UUID")
                        {
                            for (auto& propertyMap : interfaceMap.second)
                            {
                                if (propertyMap.first == "UUID")
                                {
                                    uuid = std::get_if<std::string>(
                                        &propertyMap.second);
                                }
                            }
                        }

                        if (interfaceMap.first ==
                            "xyz.openbmc_project.MCTP.Endpoint")
                        {
                            for (auto& propertyMap : interfaceMap.second)
                            {
                                if (propertyMap.first == "EID")
                                {
                                    eid = std::get_if<uint32_t>(
                                        &propertyMap.second);
                                }
                                else if (propertyMap.first ==
                                         "SupportedMessageTypes")
                                {
                                    supportedMsgTypes =
                                        std::get_if<std::vector<uint8_t>>(
                                            &propertyMap.second);
                                }
                            }
                        }
                    }

                    if (eid == nullptr)
                    {
                        BMCWEB_LOG_DEBUG(
                            "handleMctpInBandActions: EID not found");
                        messages::internalError(asyncResp->res);
                        continue;
                    }
                    if (uuid && (*uuid) == chassisUUID && supportedMsgTypes)
                    {
                        if (std::find(supportedMsgTypes->begin(),
                                      supportedMsgTypes->end(),
                                      mctpTypeVDMIANA) !=
                            supportedMsgTypes->end())
                        {
                            foundEID = true;
                            break;
                        }
                    }
                }

                if (foundEID)
                {
                    switch (option)
                    {
                        case InBandOption::BackgroundCopyStatus:
                        {
                            nlohmann::json& oem =
                                asyncResp->res.jsonValue["Oem"]["Nvidia"];
                            oem["@odata.type"] =
                                "#NvidiaChassis.v1_3_0.NvidiaRoTChassis";

                            // Calling the following methods,
                            // updateInBandEnabled, updateBackgroundCopyEnabled,
                            // and updateBackgroundCopyStatus asynchronously,
                            // may cause unpredictable behavior. These methods
                            // use 'mctp-vdm-util', which is not designed to
                            // handle more than one request at the same time.
                            // Running more than one command simultaneously may
                            // result in output from a previous (or another)
                            // request. The fix addresses this issue by changing
                            // the way the functions are called, simulating
                            // synchronous execution by invoking each command
                            // sequentially instead of simultaneously.

                            uint32_t endpointId = *eid;
                            updateInBandEnabled(req, asyncResp, endpointId,
                                                [req, asyncResp, endpointId]() {
                                updateBackgroundCopyEnabled(
                                    req, asyncResp, endpointId,
                                    [req, asyncResp, endpointId]() {
                                    updateBackgroundCopyStatus(req, asyncResp,
                                                               endpointId);
                                });
                            });
                            break;
                        }
                        case InBandOption::setBackgroundCopyEnabled:
                            enableBackgroundCopy(req, asyncResp, *eid, enabled,
                                                 chassisId);
                            break;
                        case InBandOption::setInBandEnabled:
                            enableInBand(req, asyncResp, *eid, enabled,
                                         chassisId);
                            break;
                        default:
                            BMCWEB_LOG_DEBUG(
                                "Invalid enum provided for inNand mctp access");
                            break;
                    }
                }
            },
                serviceName, "/xyz/openbmc_project/mctp",
                "org.freedesktop.DBus.ObjectManager", "GetManagedObjects");
        }
    });
}

/**
 * @brief Retrieves Oem BootStatus code for the chassis endpoint
 * @param asyncResp   Pointer to object holding response data
 * @param chassisObjPath   Path of the chassis endpoint
 */
inline void
    getOemBootStatus(const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                     const std::string chassisObjPath)
{
    static constexpr std::array<std::string_view, 1> interfaces = {
        bootStatusIntf};

    dbus::utility::getSubTree(
        "/xyz/openbmc_project/inventory/system/chassis", 0, interfaces,
        [asyncResp, chassisObjPath](
            const boost::system::error_code& ec,
            const dbus::utility::MapperGetSubTreeResponse& subtree) {
        std::string statusService{};

        if (ec)
        {
            BMCWEB_LOG_DEBUG("No D-Bus object found implementing"
                             "com.nvidia.RoT.BootStatus for {}",
                             chassisObjPath);
            return;
        }

        for (const auto& obj : subtree)
        {
            sdbusplus::message::object_path objPath(obj.first);
            if (!boost::equals(objPath.filename(), chassisObjPath))
            {
                continue;
            }

            for (const auto& [service, interfaces] : obj.second)
            {
                statusService = service;
                break;
            }
        }
        crow::connections::systemBus->async_method_call(
            [asyncResp,
             chassisObjPath](const boost::system::error_code errorCode,
                             const boost::container::flat_map<
                                 std::string, dbus::utility::DbusVariantType>&
                                 propertiesList) {
            if (errorCode)
            {
                // OK since not all fwtypes support bootstatus
                return;
            }

            const auto& it = propertiesList.find("BootStatus");
            if (it == propertiesList.end())
            {
                BMCWEB_LOG_ERROR(
                    "Can't find D-Bus property \"com.nvidia.RoT.BootStatus.BootStatus\"!");
                messages::propertyMissing(asyncResp->res, "BootStatus");
                return;
            }

            const auto* bootStatus =
                std::get_if<std::vector<uint8_t>>(&it->second);
            if (bootStatus == nullptr)
            {
                BMCWEB_LOG_ERROR(
                    "wrong types for D-Bus property \"com.nvidia.RoT.BootStatus.BootStatus\"!");
                messages::propertyValueTypeError(asyncResp->res, "",
                                                 "BootStatus");
                return;
            }
            std::string out{};

            std::ostringstream oss;
            for (auto byte : *bootStatus)
            {
                // Convert each byte to a two-character hexadecimal string
                oss << std::hex << std::setw(2) << std::setfill('0')
                    << static_cast<int>(byte);
            }
            out = "0x" + oss.str();
            asyncResp->res.jsonValue["BootStatusCode"] = out;
        },
            statusService,
            "/xyz/openbmc_project/inventory/system/chassis/" + chassisObjPath,
            "org.freedesktop.DBus.Properties", "GetAll",
            "com.nvidia.RoT.BootStatus");
    });
}

/**
 *@brief Sets the background copy for particular chassis
 *
 * @param req   Pointer to object holding request data
 * @param asyncResp   Pointer to object holding response data
 * @param chassisUUID  Chassis ID
 * @param enabled Enable or disable the background copy
 *
 * @return None.
 */
inline void setBackgroundCopyEnabled(
    const crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& chassisId, const std::string& chassisUUID, bool enabled)
{
    handleMctpInBandActions(req, asyncResp, chassisUUID,
                            InBandOption::setBackgroundCopyEnabled, enabled,
                            chassisId);
}

/**
 *@brief Sets in-band for particular chassis
 *
 * @param req   Pointer to object holding request data
 * @param asyncResp   Pointer to object holding response data
 * @param chassisUUID  Chassis ID
 *
 * @return None.
 */
inline void
    setInBandEnabled(const crow::Request& req,
                     const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                     const std::string& chassisId,
                     const std::string& chassisUUID, bool enabled)
{
    handleMctpInBandActions(req, asyncResp, chassisUUID,
                            InBandOption::setInBandEnabled, enabled, chassisId);
}

/**
 *@brief Gets background copy and in-band info for particular chassis
 *
 * @param req   Pointer to object holding request data
 * @param asyncResp   Pointer to object holding response data
 * @param chassisUUID  Chassis ID
 *
 * @return None.
 */
inline void getBackgroundCopyAndInBandInfo(
    const crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& chassisUUID)
{
    handleMctpInBandActions(req, asyncResp, chassisUUID,
                            InBandOption::BackgroundCopyStatus);
}

/**
 * @brief Get the Chassis UUID
 *
 * @param req - Pointer to object holding request data
 * @param asyncResp - Pointer to object holding response data
 * @param connectionName - connection name
 * @param path - D-Bus path
 * @param isERoT - true: ERoT resource. false: not a ERoT
 */
inline void getChassisUUID(const crow::Request& req,
                           const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                           const std::string& connectionName,
                           const std::string& path, bool isERoT = false)
{
    sdbusplus::asio::getProperty<std::string>(
        *crow::connections::systemBus, connectionName, path,
        "xyz.openbmc_project.Common.UUID", "UUID",
        [req, asyncResp, isERoT](const boost::system::error_code ec,
                                 const std::string& chassisUUID) {
        if (ec)
        {
            BMCWEB_LOG_DEBUG("DBUS response error for UUID");
            messages::internalError(asyncResp->res);
            return;
        }
        asyncResp->res.jsonValue["UUID"] = chassisUUID;

        if (isERoT)
        {
            getBackgroundCopyAndInBandInfo(req, asyncResp, chassisUUID);
        }
    });
}

inline void getChassisName(const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                           const std::string& connectionName,
                           const std::string& path)
{
    sdbusplus::asio::getProperty<std::string>(
        *crow::connections::systemBus, connectionName, path,
        "xyz.openbmc_project.Inventory.Item", "PrettyName",
        [asyncResp](const boost::system::error_code ec,
                    const std::string& chassisName) {
        if (ec)
        {
            BMCWEB_LOG_DEBUG("DBUS response error for chassis name");
            messages::internalError(asyncResp->res);
            return;
        }
        asyncResp->res.jsonValue["Name"] = chassisName;
    });
}

inline void getChassisType(const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                           const std::string& connectionName,
                           const std::string& path)
{
    sdbusplus::asio::getProperty<std::string>(
        *crow::connections::systemBus, connectionName, path,
        "xyz.openbmc_project.Inventory.Item.Chassis", "Type",
        [asyncResp](const boost::system::error_code ec,
                    const std::string& chassisType) {
        if (ec)
        {
            BMCWEB_LOG_DEBUG("DBUS response error for UUID");
            messages::internalError(asyncResp->res);
            return;
        }
        asyncResp->res.jsonValue["ChassisType"] = getChassisType(chassisType);
    });
}

inline void
    getChassisManufacturer(const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                           const std::string& connectionName,
                           const std::string& path)
{
    sdbusplus::asio::getProperty<std::string>(
        *crow::connections::systemBus, connectionName, path,
        "xyz.openbmc_project.Inventory.Decorator.Asset", "Manufacturer",
        [asyncResp](const boost::system::error_code ec,
                    const std::string& manufacturer) {
        if (ec)
        {
            BMCWEB_LOG_DEBUG("DBUS response error for Manufacturer");
            messages::internalError(asyncResp->res);
            return;
        }
        asyncResp->res.jsonValue["Manufacturer"] = manufacturer;
    });
}

inline void getChassisSKU(const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                          const std::string& connectionName,
                          const std::string& path)
{
    sdbusplus::asio::getProperty<std::string>(
        *crow::connections::systemBus, connectionName, path,
        "xyz.openbmc_project.Inventory.Decorator.Asset", "SKU",
        [asyncResp](const boost::system::error_code ec,
                    const std::string& chassisSKU) {
        if (ec)
        {
            BMCWEB_LOG_DEBUG("DBUS response error for chassisSKU");
            messages::internalError(asyncResp->res);
            return;
        }
        if (!chassisSKU.empty())
        {
            asyncResp->res.jsonValue["SKU"] = chassisSKU;
        }
    });
}

inline void
    getChassisSerialNumber(const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                           const std::string& connectionName,
                           const std::string& path)
{
    sdbusplus::asio::getProperty<std::string>(
        *crow::connections::systemBus, connectionName, path,
        "xyz.openbmc_project.Inventory.Decorator.Asset", "SerialNumber",
        [asyncResp](const boost::system::error_code ec,
                    const std::string& serialNumber) {
        if (ec)
        {
            BMCWEB_LOG_DEBUG("DBUS response error for SerialNumber");
            messages::internalError(asyncResp->res);
            return;
        }
        asyncResp->res.jsonValue["SerialNumber"] = serialNumber;
    });
}

template <typename CallbackFunc>
inline void isEROTChassis(const std::string& chassisID, CallbackFunc&& callback)
{
    const std::array<const char*, 1> interfaces = {
        "xyz.openbmc_project.Inventory.Item.SPDMResponder"};

    crow::connections::systemBus->async_method_call(
        [chassisID, callback](const boost::system::error_code ec,
                              const GetSubTreeType& subtree) {
        if (ec)
        {
            callback(false, false);
            return;
        }
        const auto objIt = std::find_if(
            subtree.begin(), subtree.end(),
            [chassisID](
                const std::pair<std::string,
                                std::vector<std::pair<
                                    std::string, std::vector<std::string>>>>&
                    object) {
            return !chassisID.compare(
                sdbusplus::message::object_path(object.first).filename());
        });
        if (objIt == subtree.end())
        {
            BMCWEB_LOG_DEBUG("Dbus Object not found:{}", chassisID);
            callback(false, false);
            return;
        }
        std::string serviceName;
        for (const auto& service : objIt->second)
        {
            if (!serviceName.empty())
            {
                break;
            }
            for (const auto& interface : service.second)
            {
                if (interface == "xyz.openbmc_project.Association.Definitions")
                {
                    serviceName = service.first;
                    break;
                }
            }
        }
        if (serviceName.empty())
        {
            callback(false, false);
            return;
        }
        sdbusplus::asio::getProperty<Associations>(
            *crow::connections::systemBus, serviceName, objIt->first,
            "xyz.openbmc_project.Association.Definitions", "Associations",
            [chassisID, callback](const boost::system::error_code ec,
                                  const Associations& associations) {
            if (ec)
            {
                callback(false, false);
                return;
            }
            for (const auto& assoc : associations)
            {
                if (std::get<1>(assoc) == "associated_ROT")
                {
                    // check if it is CPU ERoT
                    std::string path = std::get<2>(assoc);
                    size_t rotNamePos = path.rfind('/');
                    if (rotNamePos == std::string::npos ||
                        rotNamePos == (path.size() - 1))
                    {
                        callback(true, false);
                        return;
                    }

                    constexpr std::array<std::string_view, 1> cpuInterface = {
                        "xyz.openbmc_project.Inventory.Item.Cpu"};

                    dbus::utility::getSubTreePaths(
                        path.substr(0, rotNamePos), 0, cpuInterface,
                        [callback](
                            const boost::system::error_code ec2,
                            const dbus::utility::MapperGetSubTreePathsResponse&
                                subtreePaths) {
                        if ((ec2) || (subtreePaths.size() == 0))
                        {
                            callback(true, false);
                            return;
                        }
                        // It's CPU ERoT for DOT actions
                        callback(true, true);
                    });
                    return;
                }
            }
            callback(false, false);
        });
    },
        "xyz.openbmc_project.ObjectMapper",
        "/xyz/openbmc_project/object_mapper",
        "xyz.openbmc_project.ObjectMapper", "GetSubTree",
        "/xyz/openbmc_project/inventory", 0, interfaces);
}

template <typename CallbackFunc>
inline void getAssociationEndpoint(const std::string& objPath,
                                   CallbackFunc&& callback)
{
    crow::connections::systemBus->async_method_call(
        [callback, objPath](const boost::system::error_code ec,
                            std::variant<std::vector<std::string>>& resp) {
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
        std::vector<std::string>* data =
            std::get_if<std::vector<std::string>>(&resp);
        if (data == nullptr || data->size() == 0)
        {
            BMCWEB_LOG_ERROR(
                "{}(busctl call {} {} {} Get ss {} endpoints)",
                ((data == nullptr) ? "Data is null. " : "Data is empty"),
                dbus_utils::mapperBusName, objPath,
                dbus_utils::propertyInterface,
                dbus_utils::associationInterface);
            /*
                            Object must have associated inventory object.
                            Exemplary test on hardware:
                                busctl call xyz.openbmc_project.ObjectMapper
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
        std::string endpointPath = data->front();
        callback(true, endpointPath);
    },
        dbus_utils::mapperBusName, objPath, dbus_utils::propertyInterface,
        "Get", dbus_utils::associationInterface, "endpoints");
}

template <typename CallbackFunc>
inline void getRedfishURL(const std::filesystem::path& invObjPath,
                          CallbackFunc&& callback)
{
    BMCWEB_LOG_DEBUG("getRedfishURL({})", invObjPath.string());
    crow::connections::systemBus->async_method_call(
        [invObjPath, callback](const boost::system::error_code ec,
                               const GetObjectType& resp) {
        std::string url;
        if (ec || resp.empty())
        {
            BMCWEB_LOG_ERROR(
                "DBUS response error during getting of service name: {}", ec);
            callback(false, url);
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
                    url = std::string(
                              "/redfish/v1/Systems/" +
                              std::string(BMCWEB_REDFISH_SYSTEM_URI_NAME) +
                              "/Processors/") +
                          invObjPath.filename().string();
                    BMCWEB_LOG_DEBUG("{} {} => URL: {}", service, interface,
                                     url);
                    callback(true, url);
                    return;
                }
                if (interface == chassisInvInterf)
                {
                    url = std::string("/redfish/v1/Chassis/") +
                          invObjPath.filename().string();
                    BMCWEB_LOG_DEBUG("{} {} => URL: {}", service, interface,
                                     url);
                    callback(true, url);
                    return;
                }
                if (interface == nvLinkMgmtInvIntf)
                {
                    const std::string chassisPrefixDbus =
                        "/xyz/openbmc_project/inventory/system/chassis/";
                    if (invObjPath.string().find(chassisPrefixDbus) !=
                        std::string::npos)
                    {
                        std::string url = std::string("/redfish/v1/Chassis/");
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
                        std::string url;
                        if (!status)
                        {
                            BMCWEB_LOG_DEBUG(
                                "Unable to get the association endpoint");

                            callback(false, url);
                            return;
                        }
                        sdbusplus::message::object_path invObjectPath(ep);
                        const std::string& fabricID = invObjectPath.filename();

                        url = std::string("/redfish/v1/Fabrics/");
                        url += fabricID;
                        url += "/Switches/";
                        url += switchID;

                        callback(true, url);
                        return;
                    });
                    return;
                }
                if (interface == bmcInvInterf)
                {
                    url = std::string(
                        "/redfish/v1/Managers/" +
                        std::string(BMCWEB_REDFISH_MANAGER_URI_NAME));
                    BMCWEB_LOG_DEBUG("{} {} => URL: {}", service, interface,
                                     url);
                    callback(true, url);
                    return;
                }
            }
            BMCWEB_LOG_DEBUG("Not found proper interface for service {}",
                             service);
        }
        BMCWEB_LOG_ERROR("Failed to find proper URL");
        callback(false, url);
    },
        dbus_utils::mapperBusName, dbus_utils::mapperObjectPath,
        dbus_utils::mapperIntf, "GetObject", invObjPath.string(),
        std::array<const char*, 0>());
}

} // namespace chassis_utils
} // namespace redfish
