// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: Copyright OpenBMC Authors
#include "redfish.hpp"

#include "bmcweb_config.h"

#include "account_service.hpp"
#include "aggregation_service.hpp"
#include "app.hpp"
#include "bios.hpp"
#include "cable.hpp"
#include "certificate_service.hpp"
#include "chassis.hpp"
#include "environment_metrics.hpp"
#include "ethernet.hpp"
#include "event_service.hpp"
#include "eventservice_sse.hpp"
#include "fabric_adapters.hpp"
#include "fan.hpp"
#include "hypervisor_system.hpp"
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
#include "nvidia_manager_eventlog.hpp"
#include "nvidia_oem_dpu.hpp"
#include "nvidia_oem_managed_entity.hpp"
#include "nvidia_oem_managed_entity_group.hpp"
#include "nvidia_oem_managers_pmc.hpp"
#include "nvidia_oem_power_domain.hpp"
#include "nvidia_oem_power_policy.hpp"
#include "nvidia_oem_power_state_group.hpp"
#include "nvidia_oem_psc_state.hpp"
#include "nvidia_oem_psu_redundancy.hpp"
#include "nvidia_oem_psu_state.hpp"
#include "nvidia_policy.hpp"
#include "nvidia_power_reset_metrics.hpp"
#include "nvidia_processor.hpp"
#include "nvidia_protected_component.hpp"
#include "nvidia_smbios_mdr.hpp"
#include "nvidia_system_variable_spi_erase.hpp"
#include "odata.hpp"
#include "openbmc/openbmc_managers.hpp"
#include "pcie.hpp"
#include "power.hpp"
#include "power_subsystem.hpp"
#include "power_supply.hpp"
#include "processor.hpp"
#include "redfish_nvidia.hpp"
#include "redfish_v1.hpp"
#include "roles.hpp"
#include "sensors.hpp"
#include "service_root.hpp"
#include "storage.hpp"
#include "systems.hpp"
#include "systems_logservices_hostlogger.hpp"
#include "systems_logservices_postcodes.hpp"
#include "task.hpp"
#include "telemetry_service.hpp"
#include "thermal.hpp"
#include "thermal_metrics.hpp"
#include "thermal_subsystem.hpp"
#include "trigger.hpp"
#include "update_service.hpp"
#include "virtual_media.hpp"
// #ifdef BMCWEB_ENABLE_NETWORK_ADAPTERS_GENERIC
#include "network_adapters_generic.hpp"
// #endif
#include "boot_options.hpp"
#include "erot_chassis.hpp"
#include "fabric.hpp"
#include "fabric_adapters.hpp"
#include "host_interface.hpp"
#include "nvidia_debug_token.hpp"
#include "nvidia_error_injection.hpp"
#include "nvidia_power_smoothing.hpp"
#include "nvidia_workload_power_profiles.hpp"
// #ifdef BMCWEB_ENABLE_NETWORK_ADAPTERS
#include "network_adapters.hpp"
// #endif
#include "nvidia_sweinj.hpp"
#include "pcie_slots.hpp"
#include "pcieslots.hpp"
#include "ports.hpp"
#include "secure_boot.hpp"
#include "secure_boot_database.hpp"
// #ifdef BMCWEB_ENABLE_HOST_ETH_IFACE
#include "system_host_eth.hpp"
// #endif
#include "trusted_components.hpp"
// #ifdef BMCWEB_ENABLE_PROFILES_FEATURE
#include "nvidia_dpu_system_profiles.hpp"
// #endif

namespace redfish
{

RedfishService::RedfishService(App& app)
{
    requestRoutesMetadata(app);
    requestRoutesOdata(app);

    if (persistent_data::nvidia::getConfig().isTLSAuthEnabled())
    {
        requestAccountServiceRoutes(app);
    }
    if constexpr (BMCWEB_REDFISH_AGGREGATION)
    {
        requestRoutesAggregationService(app);
        requestRoutesAggregationSourceCollection(app);
        requestRoutesAggregationSource(app);
    }
    if (persistent_data::nvidia::getConfig().isTLSAuthEnabled())
    {
        requestRoutesRoles(app);
        requestRoutesRoleCollection(app);
    }

    requestRoutesServiceRoot(app);
    requestRoutesNetworkProtocol(app);
    requestEthernetInterfacesRoutes(app);

    if constexpr (BMCWEB_REDFISH_ALLOW_DEPRECATED_POWER_THERMAL)
    {
        if constexpr (BMCWEB_HOST_OS_FEATURES) // TODO: wrong macro
        {
            requestRoutesThermal(app);
            requestRoutesPower(app);
        }
    }
    if constexpr (BMCWEB_REDFISH_NEW_POWERSUBSYSTEM_THERMALSUBSYSTEM)
    {
        requestRoutesEnvironmentMetrics(app);
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
    if constexpr (BMCWEB_HOST_OS_FEATURES)
    {
        requestRoutesChassisResetAction(app);
        requestRoutesChassisResetActionInfo(app);
    }
    requestRoutesChassisDrive(app);
    requestRoutesChassisDriveName(app);
    requestRoutesUpdateService(app);
    requestRoutesStorageCollection(app);
    requestRoutesStorage(app);
    requestRoutesStorageControllerCollection(app);
    requestRoutesStorageController(app);
    requestRoutesDrive(app);
    requestRoutesCable(app);
    requestRoutesCableCollection(app);

    requestRoutesSystemLogServiceCollection(app);
    requestRoutesEventLogService(app);

    requestRoutesSystemsLogServicesPostCode(app);

    if constexpr (BMCWEB_REDFISH_DUMP_LOG)
    {
        requestRoutesSystemDumpService(app);
        requestRoutesSystemDumpEntryCollection(app);
        requestRoutesSystemDumpEntry(app);
        requestRoutesSystemDumpCreate(app);
        requestRoutesSystemDumpClear(app);

        requestRoutesBMCDumpService(app);
        requestRoutesBMCDumpEntryCollection(app);
        requestRoutesBMCDumpEntry(app);
        requestRoutesBMCDumpEntryDownload(app);
        requestRoutesSystemDumpEntryDownload(app);
        requestRoutesBMCDumpCreate(app);
        requestRoutesBMCDumpClear(app);
    }

    if constexpr (!BMCWEB_REDFISH_SYSTEM_FAULTLOG_DUMP_LOG)
    {
        requestRoutesFaultLogDumpService(app);
        requestRoutesFaultLogDumpEntryCollection(app);
        requestRoutesFaultLogDumpEntry(app);
        requestRoutesFaultLogDumpClear(app);
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
    if constexpr (BMCWEB_NVIDIA_OEM_PROPERTIES)
    {
#ifdef BMCWEB_COMMAND_SMBPBI_OOB
        requestRouteAsyncRawOobCommand(app);
#endif // BMCWEB_COMMAND_SMBPBI_OOB
        if constexpr (BMCWEB_NSM_RAW_COMMAND_ENABLE)
        {
            nvidia_manager_util::requestRouteNSMRawCommand(app);
            nvidia_manager_util::requestRouteNSMRawCommandActionInfo(app);
        }
        nvidia_manager_util::requestRoutesDebugTokenManagement(app);
    }

    requestRoutesMemoryCollection(app);
    requestRoutesMemory(app);
    requestRoutesSystems(app);

    if constexpr (BMCWEB_BIOS)
    {
        requestRoutesBiosService(app);
        requestRoutesBiosSettings(app);
        requestRoutesBiosReset(app);
    }

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
    }
    else
    {
        requestRoutesJournalEventLogEntryCollection(app);
        requestRoutesJournalEventLogEntry(app);
        requestRoutesJournalEventLogClear(app);
    }

    if constexpr (BMCWEB_REDFISH_HOST_LOGGER)
    {
        requestRoutesSystemsLogServiceHostlogger(app);
    }

    requestRoutesMessageRegistryFileCollection(app);
    requestRoutesMessageRegistryFile(app);
    requestRoutesMessageRegistry(app);
    if (persistent_data::nvidia::getConfig().isTLSAuthEnabled())
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

    requestRoutesSensorCollection(app);
    requestRoutesSensor(app);

    requestRoutesTaskMonitor(app);
    requestRoutesTaskService(app);
    requestRoutesTaskCollection(app);
    requestRoutesTask(app);
    requestRoutesEventService(app);
    requestRoutesEventServiceSse(app);
    requestRoutesEventDestinationCollection(app);
    requestRoutesEventDestination(app);
    requestRoutesFabricAdapters(app);
    requestRoutesFabricAdapterCollection(app);
    requestRoutesSubmitTestEvent(app);

    if constexpr (BMCWEB_HYPERVISOR_COMPUTER_SYSTEM)
    {
        requestRoutesHypervisorSystems(app);
    }

    requestRoutesTelemetryService(app);
    requestRoutesMetricReportDefinitionCollection(app);
    requestRoutesMetricReportDefinition(app);
    requestRoutesMetricReportCollection(app);
    requestRoutesMetricReport(app);
    if constexpr (BMCWEB_HOST_OS_FEATURES)
    {
        requestRoutesTriggerCollection(app);
        requestRoutesTrigger(app);
    }
    if constexpr (BMCWEB_NVIDIA_OEM_PROPERTIES)
    {
        requestRoutesProcessorPortHistogramCollection(app);
        requestRoutesProcessorPortHistogram(app);
        requestRoutesProcessorPortHistogramBuckets(app);
    }

    requestRoutesNvidiaSmbios(app);

    if constexpr (BMCWEB_NVIDIA_OEM_PROPERTIES)
    {
        requestRoutesProcessorResetMetrics(app);
        requestRoutesSystemOemNvidiaProcessorVariableSpiActions(app);
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

    requestRoutesNvidia(app);
    // Note, this must be the last route registered
    requestRoutesRedfish(app);

    requestRoutesOpenBmcManager(*this);
    // Nvidia OEM routes
    requestRoutesNvidiaManager(*this);

    validate();
}

} // namespace redfish
