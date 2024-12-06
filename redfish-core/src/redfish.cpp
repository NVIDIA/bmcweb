#include "redfish.hpp"

#include "bmcweb_config.h"

#include "account_service.hpp"
#include "aggregation_service.hpp"
#include "app.hpp"
#include "assembly.hpp"
#include "bios.hpp"
#include "cable.hpp"
#include "certificate_service.hpp"
#include "chassis.hpp"
#include "component_integrity.hpp"
#include "control.hpp"
#include "cper_utils.hpp"
#include "environment_metrics.hpp"
#include "ethernet.hpp"
#include "event_service.hpp"
#include "eventservice_sse.hpp"
#include "fabric_adapters.hpp"
#include "fan.hpp"
#include "hypervisor_system.hpp"
#include "leak_detection.hpp"
#include "leak_detector.hpp"
#include "log_services.hpp"
#include "manager_diagnostic_data.hpp"
#include "manager_logservices_journal.hpp"
#include "managers.hpp"
#include "memory.hpp"
#include "message_registries.hpp"
#include "metadata.hpp"
#include "metric_report.hpp"
#include "metric_report_definition.hpp"
#include "network_protocol.hpp"
#include "nvidia_cpu_debug_token.hpp"
#include "nvidia_manager_eventlog.hpp"
#include "nvidia_oem_dpu.hpp"
#include "nvidia_protected_component.hpp"
#include "odata.hpp"
#include "pcie.hpp"
#include "policy.hpp"
#include "power.hpp"
#include "power_subsystem.hpp"
#include "power_supply.hpp"
#include "processor.hpp"
#include "redfish_sessions.hpp"
#include "redfish_v1.hpp"
#include "roles.hpp"
#include "sensors.hpp"
#include "service_conditions.hpp"
#include "service_root.hpp"
#include "storage.hpp"
#include "systems.hpp"
#include "task.hpp"
#include "telemetry_service.hpp"
#include "thermal.hpp"
#include "thermal_metrics.hpp"
#include "thermal_subsystem.hpp"
#include "trigger.hpp"
#include "update_service.hpp"
#include "virtual_media.hpp"
#ifdef BMCWEB_ENABLE_NETWORK_ADAPTERS_GENERIC
#include "network_adapters_generic.hpp"
#endif
#include "boot_options.hpp"
#include "erot_chassis.hpp"
#include "fabric.hpp"
#include "fabric_adapters.hpp"
#include "host_interface.hpp"
#include "nvidia_debug_token.hpp"
#include "nvidia_error_injection.hpp"
#include "nvidia_power_smoothing.hpp"
#include "nvidia_workload_power_profiles.hpp"
#ifdef BMCWEB_ENABLE_NETWORK_ADAPTERS
#include "network_adapters.hpp"
#endif
#include "nvidia_sweinj.hpp"
#include "pcie_slots.hpp"
#include "pcieslots.hpp"
#include "ports.hpp"
#include "secure_boot.hpp"
#include "secure_boot_database.hpp"
#ifdef BMCWEB_ENABLE_HOST_ETH_IFACE
#include "system_host_eth.hpp"
#endif
#include "trusted_components.hpp"
#ifdef BMCWEB_ENABLE_PROFILES
#include "profiles.hpp"
#endif

namespace redfish
{

RedfishService::RedfishService(App& app)
{
    requestAssemblyRoutes(app);
    requestRoutesMetadata(app);
    requestPcieSlotsRoutes(app);
    requestRoutesOdata(app);
    if (persistent_data::getConfig().isTLSAuthEnabled())
    {
        requestAccountServiceRoutes(app);
    }
    if (persistent_data::getConfig().isTLSAuthEnabled())
    {
        requestRoutesRoles(app);
        requestRoutesRoleCollection(app);
    }
    if constexpr (BMCWEB_REDFISH_AGGREGATION)
    {
        requestRoutesAggregationService(app);
        requestRoutesAggregationSourceCollection(app);
        requestRoutesAggregationSource(app);
    }
    requestRoutesServiceRoot(app);
    requestRoutesNetworkProtocol(app);
    if (persistent_data::getConfig().isTLSAuthEnabled())
    {
        requestRoutesSession(app);
    }
    requestEthernetInterfacesRoutes(app);
#ifdef BMCWEB_ENABLE_LLDP_DEDICATED_PORTS
    requestDedicatedPortsInterfacesRoutes(app);
#endif

    if constexpr (BMCWEB_REDFISH_ALLOW_DEPRECATED_POWER_THERMAL)
    {
#ifdef BMCWEB_ENABLE_HOST_OS_FEATURE
        requestRoutesThermal(app);
        requestRoutesPower(app);
#endif
    }

#ifdef BMCWEB_ENABLE_NETWORK_ADAPTERS
    requestRoutesNetworkAdapters(app);
    requestRoutesNetworkDeviceFunctions(app);
    requestRoutesACDPort(app);
#endif

#ifdef BMCWEB_ENABLE_HOST_ETH_IFACE
    requestHostEthernetInterfacesRoutes(app);
#endif

#ifdef BMCWEB_ENABLE_NETWORK_ADAPTERS_GENERIC
    requestRoutesNetworkAdaptersGeneric(app);
#endif

    if constexpr (BMCWEB_REDFISH_NEW_POWERSUBSYSTEM_THERMALSUBSYSTEM)
    {
        requestRoutesEnvironmentMetrics(app);
        requestRoutesProcessorEnvironmentMetrics(app);
        requestRoutesMemoryEnvironmentMetrics(app);
        requestRoutesPowerSubsystem(app);
        requestRoutesPowerSupply(app);
        requestRoutesPowerSupplyCollection(app);
        requestRoutesThermalMetrics(app);
        requestRoutesThermalSubsystem(app);
        requestRoutesFan(app);
        requestRoutesFanCollection(app);
    }
    requestRoutesManagerCollection(app);
    requestRoutesManager(app);
    requestRoutesManagerResetAction(app);
    requestRoutesManagerResetActionInfo(app);
    requestRoutesManagerResetToDefaultsAction(app);
    requestRoutesManagerDiagnosticData(app);
    requestRoutesChassisCollection(app);
    requestRoutesChassis(app);

    if constexpr (BMCWEB_NVIDIA_OEM_PROPERTIES)
    {
        requestRoutesChassisEnvironmentMetricsClearOOBSetPoint(app);
        requestRoutesProcessorEnvironmentMetricsClearOOBSetPoint(app);
    }

#ifdef BMCWEB_ENABLE_HOST_OS_FEATURE
    requestRoutesChassisResetAction(app);
    requestRoutesChassisResetActionInfo(app);
#endif
    if constexpr (BMCWEB_NVIDIA_OEM_PROPERTIES)
    {
        requestRoutesChassisDebugToken(app);
        requestRoutesCpuDebugToken(app);
    }
    requestRoutesUpdateService(app);
    if constexpr (BMCWEB_NVIDIA_OEM_PROPERTIES)
    {
        requestRoutesSplitUpdateService(app);
    }

    requestRoutesChassisDrive(app);
    requestRoutesChassisDriveName(app);
    requestRoutesStorageCollection(app);
    requestRoutesStorage(app);
    requestRoutesStorageControllerCollection(app);
    requestRoutesStorageController(app);
    requestRoutesDrive(app);
    requestRoutesCable(app);
    requestRoutesCableCollection(app);
#if defined(BMCWEB_INSECURE_ENABLE_REDFISH_FW_TFTP_UPDATE) ||                  \
    defined(BMCWEB_ENABLE_REDFISH_FW_SCP_UPDATE) ||                            \
    defined(BMCWEB_ENABLE_REDFISH_FW_HTTP_HTTPS_UPDATE)
    requestRoutesUpdateServiceActionsSimpleUpdate(app);
#endif
    requestRoutesSoftwareInventoryCollection(app);
    requestRoutesSoftwareInventory(app);
    requestRoutesInventorySoftwareCollection(app);
    requestRoutesInventorySoftware(app);
    requestRoutesSystemLogServiceCollection(app);

#ifdef BMCWEB_ENABLE_MFG_TEST_API
    requestRoutesEventLogDiagnosticDataCollect(app);
    requestRoutesEventLogDiagnosticDataEntry(app);
#endif
    requestRoutesEventLogService(app);
    requestRoutesSELLogService(app);
    requestRoutesChassisLogServiceCollection(app);
#ifdef BMCWEB_ENABLE_NVIDIA_OEM_LOGSERVICES
    requestRoutesChassisXIDLogService(app);
    requestRoutesChassisXIDLogEntryCollection(app);
#endif

    requestRoutesPostCodesEntryAdditionalData(app);
    requestRoutesPostCodesLogService(app);
    requestRoutesPostCodesClear(app);
    requestRoutesPostCodesEntry(app);
    requestRoutesPostCodesEntryCollection(app);

    requestRoutesDebugToken(app);
    requestRoutesDebugTokenServiceEntry(app);
    requestRoutesDebugTokenServiceEntryCollection(app);
    requestRoutesDebugTokenServiceDiagnosticDataCollect(app);
    requestRoutesDebugTokenServiceDiagnosticDataEntryDownload(app);

    if constexpr (BMCWEB_REDFISH_DUMP_LOG)
    {
        requestRoutesSystemDumpService(app);
        requestRoutesSystemDumpServiceActionInfo(app);
        requestRoutesSystemDumpEntryCollection(app);
        requestRoutesSystemDumpEntry(app);
        requestRoutesSystemDumpCreate(app);
        requestRoutesSystemDumpClear(app);

        requestRoutesBMCDumpService(app);
        requestRoutesBMCDumpServiceActionInfo(app);
        requestRoutesBMCDumpEntryCollection(app);
        requestRoutesBMCDumpEntry(app);
        // Need to migrate dump-offload feature to upstream work
        // requestRoutesBMCDumpEntryDownload(app);
        requestRoutesBMCDumpCreate(app);
        requestRoutesBMCDumpClear(app);

#ifdef BMCWEB_ENABLE_REDFISH_SYSTEM_FAULTLOG_DUMP_LOG
        // Nvidia has faultlog support under system
        // requestRoutesFaultLogDumpService(app);
        // requestRoutesFaultLogDumpEntryCollection(app);
        // requestRoutesFaultLogDumpEntry(app);
        // requestRoutesFaultLogDumpClear(app);
#endif // BMCWEB_ENABLE_REDFISH_SYSTEM_FAULTLOG_DUMP_LOG
    }

#ifdef BMCWEB_ENABLE_REDFISH_FDR_DUMP_LOG
    requestRoutesSystemFDRService(app);
    requestRoutesSystemFDREntryCollection(app);
    requestRoutesSystemFDREntry(app);
    requestRoutesSystemFDREntryDownload(app);
    requestRoutesSystemFDRCreate(app);
    requestRoutesSystemFDRClear(app);
    requestRoutesSystemFDRGenBirthCert(app);
#endif

#ifdef BMCWEB_ENABLE_REDFISH_SYSTEM_FAULTLOG_DUMP_LOG
    requestRoutesSystemFaultLogService(app);
    requestRoutesSystemFaultLogEntryCollection(app);
    requestRoutesSystemFaultLogEntry(app);
    requestRoutesSystemFaultLogClear(app);
#endif
    if constexpr (!BMCWEB_REDFISH_DBUS_LOG)
    {
        requestRoutesJournalEventLogEntryCollection(app);
        requestRoutesJournalEventLogEntry(app);
        requestRoutesJournalEventLogClear(app);
    }

    requestRoutesBMCLogServiceCollection(app);

    if constexpr (BMCWEB_REDFISH_BMC_JOURNAL)
    {
        requestRoutesBMCJournalLogService(app);
    }
    if constexpr (BMCWEB_REDFISH_MANAGER_EVENT_LOG)
    {
        requestRoutesMangersEventLogService(app);
    }

    if constexpr (BMCWEB_REDFISH_CPU_LOG)
    {
        requestRoutesCrashdumpService(app);
        requestRoutesCrashdumpEntryCollection(app);
        requestRoutesCrashdumpEntry(app);
        requestRoutesCrashdumpFile(app);
        requestRoutesCrashdumpClear(app);
        requestRoutesCrashdumpCollect(app);
    }

    requestRoutesProcessorCollection(app);
    requestRoutesProcessor(app);
    requestRoutesOperatingConfigCollection(app);
    requestRoutesOperatingConfig(app);
    requestRoutesProcessorMetrics(app);
    requestRoutesProcessorMemoryMetrics(app);
    requestRoutesProcessorSettings(app);
    requestRoutesProcessorReset(app);
    if constexpr (BMCWEB_NVIDIA_OEM_PROPERTIES)
    {
        requestRoutesEdppReset(app);
        requestRoutesClearPCIeCountersActionInfo(app);
        requestRoutesPCIeClearCounter(app);
        requestRoutesNvidiaManagerResetToDefaultsAction(app);
        requestRoutesNvidiaManagerEmmcSecureErase(app);
        requestRoutesManagerEmmcSecureEraseActionInfo(app);
#ifdef BMCWEB_COMMAND_SMBPBI_OOB
        requestRouteSyncRawOobCommand(app);
        requestRouteAsyncRawOobCommand(app);
        requestRoutesNvidiaAsyncOOBRawCommandActionInfo(app);
        requestRoutesNvidiaSyncOOBRawCommandActionInfo(app);
#endif // BMCWEB_COMMAND_SMBPBI_OOB
        if constexpr (BMCWEB_NSM_RAW_COMMAND_ENABLE)
        {
            nvidia_manager_util::requestRouteNSMRawCommand(app);
            nvidia_manager_util::requestRouteNSMRawCommandActionInfo(app);
        }
    }

    requestRoutesProcessorPortCollection(app);
    requestRoutesProcessorPort(app);
    requestRoutesProcessorPortMetrics(app);
    requestRoutesProcessorPortSettings(app);
    requestRoutesMemoryCollection(app);
    requestRoutesMemory(app);
    requestRoutesMemoryMetrics(app);

    requestRoutesSystems(app);
    // The routes requestRoutesSystemActionsReset(app); and
    // requestRoutesSystemResetActionInfo(app) are enabled in
    // requestRoutesSystems(app) function as per upstream sync threfore
    // removed from here
#ifdef BMCWEB_ENABLE_BIOS
    requestRoutesBiosService(app);
    requestRoutesBiosSettings(app);
    requestRoutesBiosReset(app);
    requestRoutesBiosChangePassword(app);
    requestRoutesBootOptions(app);
    requestRoutesSecureBoot(app);
    requestRoutesSecureBootDatabase(app);
#endif
#ifdef BMCWEB_ENABLE_DPU_BIOS
    requestRoutesBiosAttrRegistryService(app);
#endif
#ifdef BMCWEB_ENABLE_HOST_IFACE
    requestHostInterfacesRoutes(app);
#endif

    if constexpr (BMCWEB_VM_NBDPROXY)
    {
        requestNBDVirtualMediaRoutes(app);
    }

    if constexpr (BMCWEB_REDFISH_DBUS_LOG)
    {
        requestRoutesDBusLogServiceActionsClear(app);
        requestRoutesDBusEventLogEntryCollection(app);
        requestRoutesDBusEventLogEntry(app);
        requestRoutesDBusEventLogEntryDownload(app);
        requestRoutesDBusSELLogEntryCollection(app);
        requestRoutesDBusSELLogEntry(app);
        requestRoutesDBusSELLogServiceActionsClear(app);
    }

    if constexpr (BMCWEB_REDFISH_HOST_LOGGER)
    {
        requestRoutesSystemHostLogger(app);
        requestRoutesSystemHostLoggerCollection(app);
        requestRoutesSystemHostLoggerLogEntry(app);
    }

    requestRoutesMessageRegistryFileCollection(app);
    requestRoutesMessageRegistryFile(app);
    requestRoutesMessageRegistry(app);
    if (persistent_data::getConfig().isTLSAuthEnabled())
    {
        requestRoutesCertificateService(app);
        requestRoutesHTTPSCertificate(app);
        requestRoutesLDAPCertificate(app);
        requestRoutesTrustStoreCertificate(app);
    }
    requestRoutesSystemPCIeFunctionCollection(app);
    requestRoutesSystemPCIeFunction(app);
    requestRoutesSystemPCIeDeviceCollection(app);
    requestRoutesSystemPCIeDevice(app);
    requestRoutesChassisPCIeFunctionCollection(app);
    requestRoutesChassisPCIeFunction(app);
    requestRoutesChassisPCIeDeviceCollection(app);
    requestRoutesChassisPCIeDevice(app);
    if constexpr (BMCWEB_NVIDIA_OEM_PROPERTIES)
    {
        requestRoutesClearPCIeAerErrorStatus(app);
    }

    requestRoutesSensorCollection(app);
    requestRoutesSensor(app);

    requestRoutesTaskMonitor(app);
    requestRoutesTaskService(app);
    requestRoutesTaskCollection(app);
    requestRoutesTask(app);
    requestRoutesEventService(app);
#ifdef BMCWEB_ENABLE_SSE
    requestRoutesEventServiceSse(app);
#endif

    requestRoutesEventDestinationCollection(app);
    requestRoutesEventDestination(app);
    requestRoutesFabricAdapters(app);
    requestRoutesFabricAdapterCollection(app);
    requestRoutesSubmitTestEvent(app);

    requestRoutesHypervisorSystems(app);

    requestRoutesTelemetryService(app);
    requestRoutesMetricReportDefinitionCollection(app);
    requestRoutesMetricReportCollection(app);
    requestRoutesMetricReportDefinition(app);
    requestRoutesMetricReport(app);

    requestRoutesFabricCollection(app);
    requestRoutesFabric(app);
    requestRoutesSwitchCollection(app);
    requestRoutesSwitch(app);
    requestRoutesNVSwitchReset(app);
    requestRoutesSwitchMetrics(app);
    requestRoutesPortCollection(app);
    requestRoutesPort(app);
    requestRoutesPortMetrics(app);
    requestRoutesEndpointCollection(app);
    requestRoutesEndpoint(app);
    requestRoutesZoneCollection(app);
    requestRoutesZone(app);
    if constexpr (BMCWEB_NVIDIA_OEM_PROPERTIES)
    {
        requestRoutesSwitchPowerMode(app);
    }

#ifdef BMCWEB_ENABLE_HOST_OS_FEATURE
    requestRoutesTriggerCollection(app);
    requestRoutesTrigger(app);
#endif

    requestRoutesEROTChassisCertificate(app);
#ifdef BMCWEB_ENABLE_DOT
    requestRoutesEROTChassisDOT(app);
#endif

#ifdef BMCWEB_ENABLE_MANUAL_BOOT_MODE
    requestRoutesEROTChassisManualBootMode(app);
#endif
    requestRoutesComponentIntegrity(app);
    requestRoutesServiceConditions(app);
    requestRoutesChassisControls(app);
    requestRoutesChassisControlsCollection(app);
    requestRoutesUpdateServiceCommitImage(app);
    requestRoutesChassisControlsReset(app);
    if constexpr (BMCWEB_NVIDIA_OEM_PROPERTIES)
    {
        requestRoutesComputeDigestPost(app);
        requestRoutesErrorInjection(app);
        if constexpr (BMCWEB_REDFISH_SW_EINJ)
        {
            nvidia::sweinj::requestRoutesSwEinjAction(app);
        }
    }

#ifdef BMCWEB_ENABLE_NVIDIA_OEM_BF_PROPERTIES
    requestRoutesNvidiaOemBf(app);
    requestRoutesNvidiaManagerSetSelCapacityAction(app);
    requestRoutesNvidiaManagerGetSelCapacity(app);
#endif
    requestRoutesTrustedComponents(app);
#ifdef BMCWEB_ENABLE_REDFISH_FW_SCP_UPDATE
    requestRoutesUpdateServicePublicKeyExchange(app);
    requestRoutesUpdateServiceRevokeAllRemoteServerPublicKeys(app);
#endif

    if constexpr (BMCWEB_NVIDIA_OEM_PROPERTIES)
    {
        requestRoutesChassisFirmwareInfo(app);
        requestRoutesProcessorPowerSmoothing(app);
        requestRoutesProcessorPowerSmoothingAdminProfile(app);
        requestRoutesProcessorPowerSmoothingPresetProfileCollection(app);
        requestRoutesProcessorPowerSmoothingPresetProfile(app);
        requestRoutesProcessorWorkloadPower(app);
        requestRoutesProcessorWorkloadPowerProfileCollection(app);
        requestRoutesProcessorWorkloadPowerProfile(app);
    }

#ifdef BMCWEB_ENABLE_REDFISH_LEAK_DETECT
    requestRoutesLeakDetection(app);
    requestRoutesLeakDetector(app);
    requestRoutesLeakDetectionPolicy(app);
    requestPolicyCollection(app);
#endif
    // Note, this must be the last route registered
    requestRoutesRedfish(app);
#ifdef BMCWEB_ENABLE_PROFILES
    requestRoutesProfiles(app);
#endif
}

} // namespace redfish
