// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: Copyright OpenBMC Authors
#pragma once

#include "async_resp.hpp"
#include "dbus_singleton.hpp"
#include "logging.hpp"

#include <nlohmann/json.hpp>
#include <sdbusplus/asio/property.hpp>
#include <sdbusplus/exception.hpp>
#include <sdbusplus/message.hpp>
#include <sdbusplus/message/native_types.hpp>

#include <memory>
#include <string>
#include <string_view>
#include <type_traits>

namespace redfish
{
namespace dbus_utils
{

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
    if (physicalContext ==
        "xyz.openbmc_project.Inventory.Decorator.Area.PhysicalContextType.DCBus")
    {
        return "DCBus";
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
        "xyz.openbmc_project.Inventory.Decorator.Location.LocationTypes.Connector")
    {
        return "Connector";
    }
    if (location ==
        "xyz.openbmc_project.Inventory.Decorator.Location.LocationTypes.Bay")
    {
        return "Bay";
    }
    if (location ==
        "xyz.openbmc_project.Inventory.Decorator.Location.LocationTypes.Unknown")
    {
        return "Unknown";
    }
    return "";
}

inline std::string toOrientation(const std::string& orientation)
{
    if (orientation ==
        "xyz.openbmc_project.Inventory.Decorator.LocationOrdinal.Orientations.FrontToBack")
    {
        return "FrontToBack";
    }
    if (orientation ==
        "xyz.openbmc_project.Inventory.Decorator.LocationOrdinal.Orientations.BackToFront")
    {
        return "BackToFront";
    }
    if (orientation ==
        "xyz.openbmc_project.Inventory.Decorator.LocationOrdinal.Orientations.TopToBottom")
    {
        return "TopToBottom";
    }
    if (orientation ==
        "xyz.openbmc_project.Inventory.Decorator.LocationOrdinal.Orientations.BottomToTop")
    {
        return "BottomToTop";
    }
    if (orientation ==
        "xyz.openbmc_project.Inventory.Decorator.LocationOrdinal.Orientations.LeftToRight")
    {
        return "LeftToRight";
    }
    if (orientation ==
        "xyz.openbmc_project.Inventory.Decorator.LocationOrdinal.Orientations.RightToLeft")
    {
        return "RightToLeft";
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

inline nlohmann::json toChannelPresence(const std::string& state)
{
    if (state == "com.nvidia.MemorySpareChannel.Presence.Present")
    {
        return 1;
    }
    if (state == "com.nvidia.MemorySpareChannel.Presence.NotPresent")
    {
        return 0;
    }
    return nullptr;
}

inline std::string toPowerBreakPerformanceState(const std::string& state)
{
    if (state == "com.nvidia.ProcessorPowerBreak.PowerBreakStates.Normal")
    {
        return "Normal";
    }
    if (state == "com.nvidia.ProcessorPowerBreak.PowerBreakStates.Throttled")
    {
        return "Throttled";
    }
    if (state == "com.nvidia.ProcessorPowerBreak.PowerBreakStates.Unknown")
    {
        return "Unknown";
    }
    return "";
}

inline std::string toReference(const std::string& reference)
{
    if (reference ==
        "xyz.openbmc_project.Inventory.Decorator.LocationReference.ReferenceAreas.Bottom")
    {
        return "Bottom";
    }
    if (reference ==
        "xyz.openbmc_project.Inventory.Decorator.LocationReference.ReferenceAreas.Front")
    {
        return "Front";
    }
    if (reference ==
        "xyz.openbmc_project.Inventory.Decorator.LocationReference.ReferenceAreas.Left")
    {
        return "Left";
    }
    if (reference ==
        "xyz.openbmc_project.Inventory.Decorator.LocationReference.ReferenceAreas.Middle")
    {
        return "Middle";
    }
    if (reference ==
        "xyz.openbmc_project.Inventory.Decorator.LocationReference.ReferenceAreas.Rear")
    {
        return "Rear";
    }
    if (reference ==
        "xyz.openbmc_project.Inventory.Decorator.LocationReference.ReferenceAreas.Right")
    {
        return "Right";
    }
    if (reference ==
        "xyz.openbmc_project.Inventory.Decorator.LocationReference.ReferenceAreas.Top")
    {
        return "Top";
    }
    if (reference ==
        "xyz.openbmc_project.Inventory.Decorator.LocationReference.ReferenceAreas.Unknown")
    {
        return "Unknown";
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

        explicit DeferTaskStruct(Callback&& callbackIn) :
            callback(std::move(callbackIn))
        {}
        // NOLINTNEXTLINE(modernize-use-equals-default)
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
    if (privilege == 0x02)
    {
        return "HostBMC";
    }
    return "None";
}

inline uint8_t toSMPBIPrivilegeType(const std::string& privilegeType)
{
    if (privilegeType == "HMC")
    {
        return 0x01;
    }
    if (privilegeType == "HostBMC")
    {
        return 0x02;
    }
    return 0x00;
}

} // namespace dbus_utils
} // namespace redfish
