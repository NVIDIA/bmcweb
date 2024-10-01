#pragma once

#include "async_resp.hpp"
#include "dbus_singleton.hpp"
#include "error_messages.hpp"
#include "logging.hpp"

#include <nlohmann/json.hpp>
#include <sdbusplus/asio/property.hpp>
#include <sdbusplus/message.hpp>
#include <sdbusplus/server.hpp>
#include <sdbusplus/unpack_properties.hpp>

#include <memory>
#include <string_view>

namespace redfish
{
namespace dbus_utils
{

using PropertyVal = std::variant<uint8_t, uint16_t, std::string,
                                 std::vector<std::string>, bool>;

struct UnpackErrorPrinter
{
    void operator()(const sdbusplus::UnpackErrorReason reason,
                    const std::string& property) const noexcept
    {
        BMCWEB_LOG_ERROR(
            "DBUS property error in property: {}, reason: {}", property,
            static_cast<std::underlying_type_t<sdbusplus::UnpackErrorReason>>(
                reason));
    }
};

constexpr const char* dbusObjManagerIntf = "org.freedesktop.DBus.ObjectManager";
constexpr const char* propertyInterface = "org.freedesktop.DBus.Properties";
constexpr const char* associationInterface = "xyz.openbmc_project.Association";
constexpr const char* mapperBusName = "xyz.openbmc_project.ObjectMapper";
constexpr const char* mapperObjectPath = "/xyz/openbmc_project/object_mapper";
constexpr const char* mapperIntf = "xyz.openbmc_project.ObjectMapper";
constexpr const char* objDeleteIntf = "xyz.openbmc_project.Object.Delete";

inline std::string getRedfishLtssmState(const std::string& state)
{
    if (state == "xyz.openbmc_project.PCIe.LTSSMState.State.Detect")
    {
        return "Detect";
    }
    if (state == "xyz.openbmc_project.PCIe.LTSSMState.State.Polling")
    {
        return "Polling";
    }
    if (state == "xyz.openbmc_project.PCIe.LTSSMState.State.Configuration")
    {
        return "Configuration";
    }
    if (state == "xyz.openbmc_project.PCIe.LTSSMState.State.Recovery")
    {
        return "Recovery";
    }
    if (state == "xyz.openbmc_project.PCIe.LTSSMState.State.RecoveryEQ")
    {
        return "RecoveryEQ";
    }
    if (state == "xyz.openbmc_project.PCIe.LTSSMState.State.L0")
    {
        return "L0";
    }
    if (state == "xyz.openbmc_project.PCIe.LTSSMState.State.L0s")
    {
        return "L0s";
    }
    if (state == "xyz.openbmc_project.PCIe.LTSSMState.State.L1")
    {
        return "L1";
    }
    if (state == "xyz.openbmc_project.PCIe.LTSSMState.State.L1_PLL_PD")
    {
        return "L1_PLL_PD";
    }
    if (state == "xyz.openbmc_project.PCIe.LTSSMState.State.L2")
    {
        return "L2";
    }
    if (state == "xyz.openbmc_project.PCIe.LTSSMState.State.L1_CPM")
    {
        return "L1_CPM";
    }
    if (state == "xyz.openbmc_project.PCIe.LTSSMState.State.L1_1")
    {
        return "L1_1";
    }
    if (state == "xyz.openbmc_project.PCIe.LTSSMState.State.L1_2")
    {
        return "L1_2";
    }
    if (state == "xyz.openbmc_project.PCIe.LTSSMState.State.HotReset")
    {
        return "HotReset";
    }
    if (state == "xyz.openbmc_project.PCIe.LTSSMState.State.Loopback")
    {
        return "Loopback";
    }
    if (state == "xyz.openbmc_project.PCIe.LTSSMState.State.Disabled")
    {
        return "Disabled";
    }
    if (state == "xyz.openbmc_project.PCIe.LTSSMState.State.LinkDown")
    {
        return "LinkDown";
    }
    if (state == "xyz.openbmc_project.PCIe.LTSSMState.State.LinkReady")
    {
        return "LinkReady";
    }
    if (state == "xyz.openbmc_project.PCIe.LTSSMState.State.LanesInSleep")
    {
        return "LanesInSleep";
    }
    if (state == "xyz.openbmc_project.PCIe.LTSSMState.State.IllegalState")
    {
        return "IllegalState";
    }
    return "";
}

inline std::string getRedfishIstMode(const std::string& mode)
{
    if (mode == "xyz.openbmc_project.Control.Mode.StateOfISTMode.Disabled")
    {
        return "Disabled";
    }
    if (mode == "xyz.openbmc_project.Control.Mode.StateOfISTMode.Enabled")
    {
        return "Enabled";
    }
    if (mode == "xyz.openbmc_project.Control.Mode.StateOfISTMode.InProgress")
    {
        return "InProgress";
    }
    return "";
}

inline std::string toIstmgrStatus(const std::string& mode)
{
    if (mode == "com.Nvidia.IstModeManager.Server.StateOfISTMode.Disabled")
    {
        return "Disabled";
    }
    if (mode == "com.Nvidia.IstModeManager.Server.StateOfISTMode.Enabled")
    {
        return "Enabled";
    }
    if (mode == "com.Nvidia.IstModeManager.Server.StateOfISTMode.InProgress")
    {
        return "InProgress";
    }
    return "";
}

inline std::string getIstmgrParam(const bool& enabled)
{
    std::string val =
        "com.Nvidia.IstModeManager.Server.StateOfISTMode.Disabled";
    if (enabled)
    {
        val = "com.Nvidia.IstModeManager.Server.StateOfISTMode.Enabled";
    }
    return val;
}

inline std::string getReqMode(const bool& enabled)
{
    std::string val = "Disabled";
    if (enabled)
    {
        val = "Enabled";
    }
    return val;
}

inline const char* toPhysicalContext(const std::string& physicalContext)
{
    if (physicalContext ==
        "xyz.openbmc_project.Inventory.Decorator.Area.PhysicalContextType.Back")
    {
        return "Back";
    }
    if (physicalContext ==
        "xyz.openbmc_project.Inventory.Decorator.Area.PhysicalContextType.Backplane")
    {
        return "Backplane";
    }
    if (physicalContext ==
        "xyz.openbmc_project.Inventory.Decorator.Area.PhysicalContextType.CPU")
    {
        return "CPU";
    }
    if (physicalContext ==
        "xyz.openbmc_project.Inventory.Decorator.Area.PhysicalContextType.Fan")
    {
        return "Fan";
    }
    if (physicalContext ==
        "xyz.openbmc_project.Inventory.Decorator.Area.PhysicalContextType.Front")
    {
        return "Front";
    }
    if (physicalContext ==
        "xyz.openbmc_project.Inventory.Decorator.Area.PhysicalContextType.GPU")
    {
        return "GPU";
    }
    if (physicalContext ==
        "xyz.openbmc_project.Inventory.Decorator.Area.PhysicalContextType.GPUSubsystem")
    {
        return "GPUSubsystem";
    }
    if (physicalContext ==
        "xyz.openbmc_project.Inventory.Decorator.Area.PhysicalContextType.ProcessorModule")
    {
        return "ProcessorModule";
    }
    if (physicalContext ==
        "xyz.openbmc_project.Inventory.Decorator.Area.PhysicalContextType.Memory")
    {
        return "Memory";
    }
    if (physicalContext ==
        "xyz.openbmc_project.Inventory.Decorator.Area.PhysicalContextType.NetworkingDevice")
    {
        return "NetworkingDevice";
    }
    if (physicalContext ==
        "xyz.openbmc_project.Inventory.Decorator.Area.PhysicalContextType.PowerSupply")
    {
        return "PowerSupply";
    }
    if (physicalContext ==
        "xyz.openbmc_project.Inventory.Decorator.Area.PhysicalContextType.StorageDevice")
    {
        return "StorageDevice";
    }
    if (physicalContext ==
        "xyz.openbmc_project.Inventory.Decorator.Area.PhysicalContextType.SystemBoard")
    {
        return "SystemBoard";
    }
    if (physicalContext ==
        "xyz.openbmc_project.Inventory.Decorator.Area.PhysicalContextType.VoltageRegulator")
    {
        return "VoltageRegulator";
    }
    if (physicalContext ==
        "xyz.openbmc_project.Inventory.Decorator.Area.PhysicalContextType.Board")
    {
        return "Board";
    }
    if (physicalContext ==
        "xyz.openbmc_project.Inventory.Decorator.Area.PhysicalContextType.Chassis")
    {
        return "Chassis";
    }
    return "";
}

inline std::string toReasonType(const std::string& reason)
{
    if (reason ==
        "xyz.openbmc_project.State.ProcessorPerformance.ThrottleReasons.SWPowerCap")
    {
        return "SWPowerCap";
    }
    if (reason ==
        "xyz.openbmc_project.State.ProcessorPerformance.ThrottleReasons.HWSlowdown")
    {
        return "HWSlowdown";
    }
    if (reason ==
        "xyz.openbmc_project.State.ProcessorPerformance.ThrottleReasons.HWThermalSlowdown")
    {
        return "HWThermalSlowdown";
    }

    if (reason ==
        "xyz.openbmc_project.State.ProcessorPerformance.ThrottleReasons.HWPowerBrakeSlowdown")
    {
        return "HWPowerBrakeSlowdown";
    }
    if (reason ==
        "xyz.openbmc_project.State.ProcessorPerformance.ThrottleReasons.SyncBoost")
    {
        return "SyncBoost";
    }
    if (reason ==
        "xyz.openbmc_project.State.ProcessorPerformance.ThrottleReasons.ClockOptimizedForPower")
    {
        return "ClockOptimizedForPower";
    }
    if (reason ==
        "xyz.openbmc_project.State.ProcessorPerformance.ThrottleReasons.ClockOptimizedForThermalEngage")
    {
        return "ClockOptimizedForThermalEngage";
    }
    if (reason ==
        "xyz.openbmc_project.State.ProcessorPerformance.ThrottleReasons.GPUThermalOvertTreshold")
    {
        return "Current GPU temperature above the GPU Max Operating Temperature or Current memory temperature above the Memory Max Operating Temperature";
    }
    if (reason ==
        "xyz.openbmc_project.State.ProcessorPerformance.ThrottleReasons.None")
    {
        return "NA";
    }

    return "";
}

inline std::string toPowerSystemInputType(const std::string& state)
{
    if (state ==
        "xyz.openbmc_project.State.Decorator.PowerSystemInputs.Status.Good")
    {
        return "Normal";
    }
    if (state ==
        "xyz.openbmc_project.State.Decorator.PowerSystemInputs.Status.Fault")
    {
        return "Fault";
    }
    if (state ==
        "xyz.openbmc_project.State.Decorator.PowerSystemInputs.Status.InputOutOfRange")
    {
        return "OutOfRange";
    }
    if (state ==
        "xyz.openbmc_project.State.Decorator.PowerSystemInputs.Status.Unknown")
    {
        return "Unknown";
    }
    return "";
}

inline std::string toPerformanceStateType(const std::string& state)
{
    if (state ==
        "xyz.openbmc_project.State.ProcessorPerformance.PerformanceStates.Normal")
    {
        return "Normal";
    }
    if (state ==
        "xyz.openbmc_project.State.ProcessorPerformance.PerformanceStates.Throttled")
    {
        return "Throttled";
    }
    if (state ==
        "xyz.openbmc_project.State.ProcessorPerformance.PerformanceStates.Degraded")
    {
        return "Degraded";
    }
    if (state ==
        "xyz.openbmc_project.State.ProcessorPerformance.PerformanceStates.Unknown")
    {
        return "Unknown";
    }
    return "";
}

inline std::string toLocationType(const std::string& location)
{
    if (location ==
        "xyz.openbmc_project.Inventory.Decorator.Location.LocationTypes.Slot")
    {
        return "Slot";
    }
    if (location ==
        "xyz.openbmc_project.Inventory.Decorator.Location.LocationTypes.Embedded")
    {
        return "Embedded";
    }
    if (location ==
        "xyz.openbmc_project.Inventory.Decorator.Location.LocationTypes.Socket")
    {
        return "Socket";
    }
    if (location ==
        "xyz.openbmc_project.Inventory.Decorator.Location.LocationTypes.Backplane")
    {
        return "Backplane";
    }
    if (location ==
        "xyz.openbmc_project.Inventory.Decorator.Location.LocationTypes.Unknown")
    {
        return "Unknown";
    }
    return "";
}

inline std::string toPowerSupplyType(const std::string& powerSupply)
{
    if (powerSupply ==
        "com.nvidia.PowerSupply.PowerSupplyInfo.PowerSupplyTypes.AC")
    {
        return "AC";
    }
    if (powerSupply ==
        "com.nvidia.PowerSupply.PowerSupplyInfo.PowerSupplyTypes.DC")
    {
        return "DC";
    }
    if (powerSupply ==
        "com.nvidia.PowerSupply.PowerSupplyInfo.PowerSupplyTypes.ACorDC")
    {
        return "ACorDC";
    }
    if (powerSupply ==
        "com.nvidia.PowerSupply.PowerSupplyInfo.PowerSupplyTypes.DCRegulator")
    {
        return "DCRegulator";
    }
    if (powerSupply ==
        "com.nvidia.PowerSupply.PowerSupplyInfo.PowerSupplyTypes.Invalid")
    {
        return "Invalid";
    }
    return "";
}
/**
 * @brief Defer the callback function until the shared_ptr destroys the returned
 * object.
 * @param callback A callback function, [](boost::system::error_code ec)
 */
template <typename Callback>
inline auto deferTask(Callback&& callback)
{
    struct DeferTaskStruct
    {
        DeferTaskStruct() = delete;
        DeferTaskStruct(const DeferTaskStruct&) = delete;
        DeferTaskStruct& operator=(const DeferTaskStruct&) = delete;
        DeferTaskStruct(DeferTaskStruct&&) = delete;
        DeferTaskStruct& operator=(DeferTaskStruct&&) = delete;

        DeferTaskStruct(Callback&& callback) : callback(callback) {}

        ~DeferTaskStruct()
        {
            callback(ec);
        }

        Callback callback;
        boost::system::error_code ec;
    };
    return std::make_shared<DeferTaskStruct>(std::forward<Callback>(callback));
}

inline std::string toSMPBIPrivilegeString(uint8_t privilege)
{
    if (privilege == 0x01)
    {
        return "HMC";
    }
    else if (privilege == 0x02)
    {
        return "HostBMC";
    }
    else
    {
        return "None";
    }
}

inline uint8_t toSMPBIPrivilegeType(const std::string& privilegeType)
{
    if (privilegeType == "HMC")
    {
        return 0x01;
    }
    else if (privilegeType == "HostBMC")
    {
        return 0x02;
    }
    else
    {
        return 0x00;
    }
}

} // namespace dbus_utils

namespace details
{
void afterSetProperty(const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                      const std::string& redfishPropertyName,
                      const nlohmann::json& propertyValue,
                      const boost::system::error_code& ec,
                      const sdbusplus::message_t& msg);

void afterSetPropertyAction(const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                            const std::string& redfishActionName,
                            const std::string& redfishActionParameterName,
                            const boost::system::error_code& ec,
                            const sdbusplus::message_t& msg);
} // namespace details

template <typename PropertyType>
void setDbusProperty(const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                     std::string_view redfishPropertyName,
                     std::string_view processName,
                     const sdbusplus::message::object_path& path,
                     std::string_view interface, std::string_view dbusProperty,
                     const PropertyType& prop)
{
    std::string processNameStr(processName);
    std::string interfaceStr(interface);
    std::string dbusPropertyStr(dbusProperty);

    sdbusplus::asio::setProperty(
        *crow::connections::systemBus, processNameStr, path.str, interfaceStr,
        dbusPropertyStr, prop,
        [asyncResp, redfishPropertyNameStr = std::string{redfishPropertyName},
         jsonProp = nlohmann::json(prop)](const boost::system::error_code& ec,
                                          const sdbusplus::message_t& msg) {
        details::afterSetProperty(asyncResp, redfishPropertyNameStr, jsonProp,
                                  ec, msg);
    });
}

template <typename DbusPropertyType>
void setDbusPropertyAction(const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                           std::string_view processName,
                           const sdbusplus::message::object_path& path,
                           std::string_view interface,
                           std::string_view dbusProperty,
                           std::string_view redfishActionParameterName,
                           std::string_view redfishActionName,
                           const DbusPropertyType& prop)
{
    std::string processNameStr(processName);
    std::string interfaceStr(interface);
    std::string dbusPropertyStr(dbusProperty);

    sdbusplus::asio::setProperty(
        *crow::connections::systemBus, processNameStr, path.str, interfaceStr,
        dbusPropertyStr, prop,
        [asyncResp,
         redfishActionParameterName = std::string{redfishActionParameterName},
         jsonProp = nlohmann::json(prop),
         redfishActionNameStr = std::string{redfishActionName}](
            const boost::system::error_code& ec,
            const sdbusplus::message_t& msg) {
        details::afterSetPropertyAction(asyncResp, redfishActionNameStr,
                                        redfishActionParameterName, ec, msg);
    });
}

} // namespace redfish
