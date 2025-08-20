// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: Copyright OpenBMC Authors
#include "redfish.hpp"

#include "bmcweb_config.h"

#include "account_service.hpp"
#include "aggregation_service.hpp"
#include "app.hpp"
#include "bios.hpp"
#include "boot_options.hpp"
#include "cable.hpp"
#include "certificate_service.hpp"
#include "chassis.hpp"
#include "environment_metrics.hpp"
#include "erot_chassis.hpp"
#include "ethernet.hpp"
#include "event_service.hpp"
#include "eventservice_sse.hpp"
#include "fabric.hpp"
#include "fabric_adapters.hpp"
#include "fan.hpp"
#include "host_interface.hpp"
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
#include "network_adapters.hpp"
#include "network_adapters_generic.hpp"
#include "network_protocol.hpp"
#include "nvidia_bios.hpp"
#include "nvidia_debug_token.hpp"
#include "nvidia_dpu_system_profiles.hpp"
#include "nvidia_error_injection.hpp"
#include "nvidia_manager_eventlog.hpp"
#include "nvidia_policy.hpp"
#include "nvidia_power_reset_metrics.hpp"
#include "nvidia_power_smoothing.hpp"
#include "nvidia_processor.hpp"
#include "nvidia_protected_component.hpp"
#include "nvidia_smbios_mdr.hpp"
#include "nvidia_sweinj.hpp"
#include "nvidia_system_variable_spi_erase.hpp"
#include "nvidia_workload_power_profiles.hpp"
#include "odata.hpp"
#include "openbmc/openbmc_managers.hpp"
#include "pcie.hpp"
#include "pcie_slots.hpp"
#include "pcieslots.hpp"
#include "ports.hpp"
#include "power.hpp"
#include "power_subsystem.hpp"
#include "power_supply.hpp"
#include "processor.hpp"
#include "redfish_nvidia.hpp"
#include "redfish_sessions.hpp"
#include "redfish_v1.hpp"
#include "roles.hpp"
#include "secure_boot.hpp"
#include "secure_boot_database.hpp"
#include "sensors.hpp"
#include "service_root.hpp"
#include "storage.hpp"
#include "system_host_eth.hpp"
#include "systems.hpp"
#include "systems_logservices_hostlogger.hpp"
#include "systems_logservices_postcodes.hpp"
#include "task.hpp"
#include "telemetry_service.hpp"
#include "thermal.hpp"
#include "thermal_metrics.hpp"
#include "thermal_subsystem.hpp"
#include "trigger.hpp"
#include "trusted_components.hpp"
#include "update_service.hpp"
#include "virtual_media.hpp"
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
    if (persistent_data::nvidia::getConfig().isTLSAuthEnabled())
    {
        requestRoutesSession(app);
    }
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

    requestRoutesMemoryCollection(app);
    requestRoutesMemory(app);
    requestRoutesSystems(app);

    if constexpr (BMCWEB_BIOS)
    {
        requestRoutesBiosService(app);
        requestRoutesBiosReset(app);
    }

    requestRoutesOemBiosResetService(app);

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

    requestRoutesNvidia(app);
    // Note, this must be the last route registered
    requestRoutesRedfish(app);

    requestRoutesOpenBmcManager(*this);
    // Nvidia OEM routes
    requestRoutesNvidiaManager(*this);

    validate();
}

} // namespace redfish
