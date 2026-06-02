#include "redfish_nvidia.hpp"

#include "bmcweb_config.h"

#include "app.hpp"
#include "boot_options.hpp"
#include "component_integrity.hpp"
#include "control.hpp"
#include "erot_chassis.hpp"
#include "fabric.hpp"
#include "host_interface.hpp"
#include "leak_detection.hpp"
#include "log_services_manufacturing_test.hpp"
#include "manager_usb_ports.hpp"
#include "memory.hpp"
#include "network_adapter.hpp"
#include "nvidia_bios.hpp"
#include "nvidia_chassis.hpp"
#include "nvidia_chassis_env_metrics.hpp"
#include "nvidia_cpu_debug_token.hpp"
#include "nvidia_cpu_ist.hpp"
#include "nvidia_dot.hpp"
#include "nvidia_dot_backup_data.hpp"
#include "nvidia_dpu_system_profiles.hpp"
#include "nvidia_emmc_fullsecureerase.hpp"
#include "nvidia_error_injection.hpp"
#include "nvidia_fabric.hpp"
#include "nvidia_fabric_config_update.hpp"
#include "nvidia_leak_detector.hpp"
#include "nvidia_log_services.hpp"
#include "nvidia_log_services_debug_token.hpp"
#include "nvidia_log_services_fault.hpp"
#include "nvidia_log_services_fdr.hpp"
#include "nvidia_log_services_sel.hpp"
#include "nvidia_log_services_xid.hpp"
#include "nvidia_manager_dot.hpp"
#include "nvidia_manager_eventlog.hpp"
#include "nvidia_managers.hpp"
#include "nvidia_memory_env_metrics.hpp"
// Nvidia code starts here
#include "nvidia_multipart_update.hpp"
// Nvidia code ends here
#include "nvidia_network_adapters.hpp"
// NOLINTNEXTLINE(misc-include-cleaner)
#include "nvidia_nic_debug_token.hpp"
#include "nvidia_oem_chassis_recovery.hpp"
// NOLINTNEXTLINE(misc-include-cleaner)
#include "nvidia_oem_chassis_spi.hpp"
#include "nvidia_oem_device_reset.hpp"
#include "nvidia_oem_dpu.hpp"
#include "nvidia_oem_l1reset.hpp"
#include "nvidia_oem_managed_entity.hpp"
#include "nvidia_oem_managed_entity_group.hpp"
#include "nvidia_oem_managers_pmc.hpp"
#include "nvidia_oem_power_domain.hpp"
#include "nvidia_oem_power_policy.hpp"
#include "nvidia_oem_power_state_group.hpp"
#include "nvidia_oem_psc_state.hpp"
#include "nvidia_oem_psu_redundancy.hpp"
#include "nvidia_oem_psu_state.hpp"
#include "nvidia_pcore_dump.hpp"
#include "nvidia_policy.hpp"
#include "nvidia_power_reset_metrics.hpp"
#include "nvidia_power_smoothing.hpp"
#include "nvidia_processor.hpp"
#include "nvidia_processor_env_metrics.hpp"
#include "nvidia_processor_port.hpp"
#include "nvidia_processor_routes.hpp"
#include "nvidia_protected_component.hpp"
#include "nvidia_refresh_inventory.hpp"
#include "nvidia_sensors.hpp"
#include "nvidia_storage.hpp"
#include "nvidia_sweinj.hpp"
#include "nvidia_system.hpp"
#include "nvidia_system_processor_power_limits.hpp"
#include "nvidia_systems_logservices_hostlogger.hpp"
#include "nvidia_task.hpp"
#include "nvidia_unified_debug_token.hpp"
#include "nvidia_update_service.hpp"
#include "nvidia_workload_power_profiles.hpp"
#include "nvidia_write_protect_domains.hpp"
#include "pcieslots.hpp"
#include "ports.hpp"
#include "secure_boot.hpp"
#include "secure_boot_database.hpp"
#include "service_conditions.hpp"
#include "switch_port_pcie_equalization.hpp"
#include "system_host_eth.hpp"
#include "trusted_components.hpp"
#include "utils/nvidia_manager_utils.hpp"
#include "utils/nvidia_pcie_utils.hpp"

namespace redfish
{
void requestRoutesNvidia(crow::App& app)
{
    requestPcieSlotsRoutes(app);
    requestRoutesSensorPatch(app);

    if constexpr (BMCWEB_LLDP_DEDICATED_PORTS)
    {
        requestDedicatedPortsInterfacesRoutes(app);
    }

    if constexpr (BMCWEB_MANUFACTURING_TEST)
    {
        requestRoutesEventLogDiagnosticDataCollect(app);
        requestRoutesEventLogDiagnosticDataEntry(app);
    }

    if constexpr (BMCWEB_DOT_SUPPORT)
    {
        requestRoutesEROTChassisDOT(app);
    }

    if constexpr (BMCWEB_MANUAL_BOOT_MODE_SUPPORT)
    {
        requestRoutesEROTChassisManualBootMode(app);
    }

    if constexpr (BMCWEB_NVIDIA_OEM_BF_PROPERTIES)
    {
        requestRoutesNvidiaOemBf(app);
        requestRoutesNvidiaManagerSetSelCapacityAction(app);
        requestRoutesNvidiaManagerGetSelCapacity(app);
    }

    if constexpr (BMCWEB_REDFISH_NEW_POWERSUBSYSTEM_THERMALSUBSYSTEM)
    {
        requestRoutesProcessorEnvironmentMetrics(app);
        requestRoutesMemoryEnvironmentMetrics(app);
        requestRoutesEnvironmentMetricsPatch(app);
    }

    if constexpr (BMCWEB_NVIDIA_OEM_PROPERTIES)
    {
        if constexpr (BMCWEB_COMMAND_SMBPBI_OOB)
        {
            requestRouteAsyncRawOobCommand(app);
            requestRoutesNvidiaAsyncOOBRawCommandActionInfo(app);
            requestRoutesNvidiaSyncOOBRawCommandActionInfo(app);
            requestRouteSyncRawOobCommand(app);
        }

        requestRoutesChassisDebugToken(app);
        requestRoutesUnifiedDebugToken(app);
        requestRoutesCpuDebugToken(app);
        requestRoutesChassisEnvironmentMetricsClearOOBSetPoint(app);
        requestRoutesChassisFirmwareInfo(app);
        requestRoutesClearPCIeCountersActionInfo(app);
        // requestRoutesComputeDigestPost(app);
        requestRoutesEdppReset(app);
        requestRoutesErrorInjection(app);
        requestRoutesChassisPowerSmoothing(app);
        requestRoutesManagerEmmcSecureEraseActionInfo(app);
        requestRoutesNvidiaManagerEmmcSecureErase(app);
        requestRoutesManagerEmmcFullSecureEraseActionInfo(app);
        requestRoutesNvidiaManagerEmmcFullSecureErase(app);
        requestRoutesNvidiaManagerResetToDefaultsAction(app);
        requestRoutesPCIeClearCounter(app);
        requestRoutesProcessorEnvironmentMetricsClearOOBSetPoint(app);
        requestRoutesProcessorPowerSmoothing(app);
        requestRoutesProcessorPowerSmoothingAdminProfile(app);
        requestRoutesProcessorPowerSmoothingPresetProfile(app);
        requestRoutesProcessorPowerSmoothingPresetProfileCollection(app);
        requestRoutesProcessorWorkloadPower(app);
        requestRoutesProcessorWorkloadPowerProfile(app);
        requestRoutesProcessorWorkloadPowerProfileCollection(app);
        requestRoutesSwitchPowerMode(app);
        requestRoutesClearPCIeAerErrorStatus(app);
        requestRoutesSwitchHistogramCollection(app);
        requestRoutesSwitchHistogram(app);
        requestRoutesSwitchHistogramBuckets(app);
        requestRoutesSwitchPortHistogramCollection(app);
        requestRoutesSwitchPortHistogram(app);
        requestRoutesSwitchPortHistogramBuckets(app);
        requestRoutesWriteProtectDomain(app);
    }

    if constexpr (BMCWEB_NETWORK_ADAPTERS)
    {
        requestRoutesNetworkAdaptersLegacy(app);
        requestRoutesNetworkDeviceFunctionsLegacy(app);
        requestRoutesACDPortLegacy(app);
    }

    if constexpr (BMCWEB_HOST_ETH_IFACE)
    {
        requestHostEthernetInterfacesRoutes(app);
    }

    if constexpr (BMCWEB_NETWORK_ADAPTERS_GENERIC)
    {
        requestRoutesChassisNetworkAdapter(app);
        if constexpr (BMCWEB_NVIDIA_OEM_PROPERTIES)
        {
            requestRoutesNetworkAdapterPortHistogramLegacy(app);
        }
    }

    if constexpr (BMCWEB_REDFISH_DBUS_LOG)
    {
        requestRoutesSELLogService(app);
        requestRoutesDBusSELLogEntryCollection(app);
        requestRoutesDBusSELLogEntry(app);
        requestRoutesDBusSELLogServiceActionsClear(app);
    }

    if constexpr (BMCWEB_NVIDIA_OEM_LOGSERVICES)
    {
        requestRoutesChassisXIDLogService(app);
        requestRoutesChassisXIDLogEntryCollection(app);
    }

    if constexpr (BMCWEB_REDFISH_HOST_LOGGER)
    {
        requestRoutesSystemsLogServiceHostloggerDownload(app);
    }

    requestRoutesDebugToken(app);
    requestRoutesDebugTokenServiceEntry(app);
    requestRoutesDebugTokenServiceEntryCollection(app);
    requestRoutesDebugTokenServiceDiagnosticDataCollect(app);
    requestRoutesDebugTokenServiceDiagnosticDataEntryDownload(app);

    requestRoutesNvidiaUpdateService(app);

    if constexpr (BMCWEB_REDFISH_FDR_LOG)
    {
        requestRoutesSystemFDRService(app);
        requestRoutesSystemFDREntryCollection(app);
        requestRoutesSystemFDREntry(app);
        requestRoutesSystemFDREntryDownload(app);
        requestRoutesSystemFDRCreate(app);
        requestRoutesSystemFDRClear(app);
        requestRoutesSystemFDRGenBirthCert(app);
    }

    requestRoutesEventLogServicePatch(app);
    requestRoutesChassisLogServiceCollection(app);

    if constexpr (BMCWEB_REDFISH_SYSTEM_FAULTLOG_DUMP_LOG)
    {
        requestRoutesSystemFaultLogService(app);
        requestRoutesSystemFaultLogEntryCollection(app);
        requestRoutesSystemFaultLogEntry(app);
        requestRoutesSystemFaultLogClear(app);
    }

    if constexpr (BMCWEB_REDFISH_DUMP_LOG)
    {
        requestRoutesSystemDumpServiceActionInfo(app);
        requestRoutesBMCDumpServiceActionInfo(app);
    }

    // The SetProcessorPowerLimits OEM action is advertised on the
    // ComputerSystem GET only when BMCWEB_NVIDIA_OEM_PROPERTIES is enabled
    // (systems.hpp); gate the route registration to match, consistent with the
    // other OEM route blocks above.
    if constexpr (BMCWEB_NVIDIA_OEM_PROPERTIES)
    {
        requestRoutesProcessorPowerLimits(app);
    }
    requestRoutesProcessorMetrics(app);
    requestRoutesProcessorMemoryMetrics(app);
    requestRoutesProcessorSettings(app);
    requestRoutesProcessorReset(app);

    requestRoutesProcessorPortCollection(app);
    requestRoutesProcessorPort(app);
    requestRoutesProcessorPortMetrics(app);
    requestRoutesProcessorPortSettings(app);

    requestRoutesMemoryMetrics(app);

    if constexpr (BMCWEB_BIOS)
    {
        requestRoutesOemBios(app);
        requestRoutesBiosChangePassword(app);
        requestRoutesBiosSettings(app);
        requestRoutesBootOptions(app);
        requestRoutesSecureBoot(app);
        requestRoutesSecureBootDatabase(app);
    }
    requestRoutesOemBiosResetService(app);
    if constexpr (BMCWEB_DPU_BIOS)
    {
        requestRoutesBiosAttrRegistryService(app);
    }
    if constexpr (BMCWEB_HOST_IFACE)
    {
        requestHostInterfacesRoutes(app);
    }

    requestRoutesChassisPCIeFunctionCollection(app);
    requestRoutesChassisPCIeFunction(app);
    requestRoutesChassisPCIeDeviceCollection(app);
    requestRoutesChassisPCIeDevice(app);

    requestRoutesFabricCollection(app);
    requestRoutesFabric(app);
    requestRoutesNvidiaConfigFile(app);
    requestRoutesSwitchCollection(app);
    requestRoutesSwitch(app);
    requestRoutesNVSwitchReset(app);
    requestRoutesSwitchMetrics(app);
    requestRoutesPCIeEqualization(app);

    if constexpr (BMCWEB_REDFISH_ROUTES_PORT)
    {
        requestRoutesPortCollection(app);
        requestRoutesPort(app);
        requestRoutesPortMetrics(app);
    }
    if constexpr (BMCWEB_REDFISH_ROUTES_ENDPOINT)
    {
        requestRoutesEndpointCollection(app);
        requestRoutesEndpoint(app);
    }
    if constexpr (BMCWEB_REDFISH_ROUTES_ZONE)
    {
        requestRoutesZoneCollection(app);
        requestRoutesZone(app);
    }

    requestRoutesEROTChassisCertificate(app);

    requestRoutesComponentIntegrity(app);
    requestRoutesServiceConditions(app);
    requestRoutesChassisControls(app);
    requestRoutesChassisControlsCollection(app);
    requestRoutesChassisControlsReset(app);
    requestRoutesChassisSetCPURecoveryMode(app);
    requestRoutesTrustedComponents(app);
    requestRoutesNvidiaOemDOT(app);
    requestRoutesDOTBackupDataCollection(app);

    if constexpr (BMCWEB_NVIDIA_DOT_KEYD_SUPPORT)
    {
        requestRoutesManagerDOT(app);
    }

    if constexpr (BMCWEB_CPU_IST)
    {
        requestRoutesIst(app);
        requestRoutesIstActionInfo(app);
    }

    if constexpr (BMCWEB_REDFISH_LEAK_DETECT)
    {
        requestRoutesLeakDetection(app);
        requestRoutesLeakDetector(app);
    }

    if constexpr (BMCWEB_NVIDIA_OEM_POLICIES)
    {
        requestPolicyCollection(app);
        if constexpr (BMCWEB_REDFISH_LEAK_DETECT)
        {
            requestRoutesLeakDetectionPolicy(app);
        }
    }

    if constexpr (BMCWEB_PROFILE_CONFIGURATION)
    {
        requestRoutesProfiles(app);
    }

    if constexpr (BMCWEB_NSM_RAW_COMMAND_ENABLE)
    {
        nvidia_manager_util::requestRouteNSMRawCommand(app);
        nvidia_manager_util::requestRouteNSMRawCommandActionInfo(app);
    }

    if constexpr (BMCWEB_REDFISH_MANAGER_EVENT_LOG)
    {
        requestRoutesMangersEventLogService(app);
    }

    if constexpr (BMCWEB_REDFISH_SW_EINJ)
    {
        nvidia::sweinj::requestRoutesSwEinjAction(app);
    }

    if constexpr (BMCWEB_NVIDIA_OEM_PROPERTIES)
    {
        requestRoutesProcessorPortHistogramCollection(app);
        requestRoutesProcessorPortHistogram(app);
        requestRoutesProcessorPortHistogramBuckets(app);
    }

    if constexpr (BMCWEB_NVIDIA_OEM_PROPERTIES)
    {
        nvidia_manager_util::requestRoutesDebugTokenManagement(app);
    }

    if constexpr (BMCWEB_CPU_DIAG_SUPPORT)
    {
        requestRoutesSystemsCPUDiag(app);
    }

    if constexpr (BMCWEB_NVIDIA_PCORE_DUMP)
    {
        requestRoutesCollectPCoreDump(app);
    }

    if constexpr (BMCWEB_NVIDIA_OEM_L1RESET)
    {
        requestRoutesSystemsOemNvidiaL1Reset(app);
    }

    if constexpr (BMCWEB_NVIDIA_OEM_PROPERTIES)
    {
        requestRoutesProcessorResetMetrics(app);
        requestRoutesChassisOemNvidiaProcessorVariableSpiActions(app);
        requestRoutesChassisOemNvidiaRecoveryActions(app);
        requestRoutesRefreshInventory(app);
        requestRoutesDeviceReset(app);
        requestRoutesPortResetTransceiver(app);
    }

    if constexpr (BMCWEB_NVIDIA_OEM_PMC)
    {
        nvidia_oem_managers_pmc::requestRoutesNvidiaPowerCompliance(app);
        nvidia_oem_managers_pmc::
            requestRoutesNvidiaPowerComplianceManagerActions(app);
        nvidia_oem_power_domain::requestRoutesNvidiaPowerDomain(app);
        nvidia_oem_power_policy::requestRoutesNvidiaPowerPolicy(app);
        nvidia_oem_power_state_group::requestRoutesNvidiaPowerStateGroup(app);
        nvidia_oem_psc_state::requestRoutesNvidiaPscState(app);
        nvidia_oem_psu_state::requestRoutesNvidiaPsuState(app);
        nvidia_oem_psu_redundancy::requestRoutesNvidiaPsuRedundancy(app);
        nvidia_oem_managed_entity_group::requestRoutesNvidiaManagedEntityGroup(
            app);
        nvidia_oem_managed_entity::requestRoutesNvidiaManagedEntity(app);
    }
    requestRoutesTaskUpdate(app);
    requestRoutesNvidiaChassisDriveName(app);
    requestRoutesNvidiaDrive(app);

    if constexpr (BMCWEB_MANAGER_USB_PORTS)
    {
        manager_usb_ports::requestRoutesManagerUSBPorts(app);
    }
    // Nvidia code starts here

    nvidia::requestRoutesNvUpdateServiceMultipartUpdate(app);
    // Nvidia code ends here
}

} // namespace redfish
