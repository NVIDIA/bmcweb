#include "redfish_nvidia.hpp"

#include "bmcweb_config.h"

#include "assembly.hpp"
#include "bios.hpp"
#include "boot_options.hpp"
#include "control.hpp"
#include "environment_metrics.hpp"
#include "erot_chassis.hpp"
#include "fabric.hpp"
#include "host_interface.hpp"
#include "leak_detection.hpp"
#include "leak_detector.hpp"
#include "log_services_manufacturing_test.hpp"
#include "memory.hpp"
#include "network_adapters.hpp"
#include "network_adapters_generic.hpp"
#include "nvidia_debug_token.hpp"
#include "nvidia_error_injection.hpp"
#include "nvidia_log_services.hpp"
#include "nvidia_log_services_debug_token.hpp"
#include "nvidia_log_services_fault.hpp"
#include "nvidia_log_services_fdr.hpp"
#include "nvidia_log_services_sel.hpp"
#include "nvidia_log_services_xid.hpp"
#include "nvidia_managers.hpp"
#include "nvidia_oem_dpu.hpp"
#include "nvidia_policy.hpp"
#include "nvidia_power_smoothing.hpp"
#include "nvidia_processor_port.hpp"
#include "nvidia_protected_component.hpp"
#include "nvidia_sweinj.hpp"
#include "nvidia_update_service.hpp"
#include "nvidia_workload_power_profiles.hpp"
#include "pcie.hpp"
#include "pcieslots.hpp"
#include "ports.hpp"
#include "processor.hpp"
#include "profiles.hpp"
#include "secure_boot.hpp"
#include "secure_boot_database.hpp"
#include "service_conditions.hpp"
#include "system_host_eth.hpp"
#include "trigger.hpp"
#include "trusted_components.hpp"
#include "update_service.hpp"
#include "nvidia_fabric.hpp"

namespace redfish
{
void requestRoutesNvidia(crow::App& app)
{
    requestAssemblyRoutes(app);
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

    if constexpr (BMCWEB_SCP_UPDATE)
    {
        requestRoutesUpdateServicePublicKeyExchange(app);
        requestRoutesUpdateServiceRevokeAllRemoteServerPublicKeys(app);
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
        requestRoutesChassisEnvironmentMetricsClearOOBSetPoint(app);
        requestRoutesChassisFirmwareInfo(app);
        requestRoutesClearPCIeCountersActionInfo(app);
        requestRoutesComputeDigestPost(app);
        requestRoutesEdppReset(app);
        requestRoutesErrorInjection(app);
        requestRoutesManagerEmmcSecureEraseActionInfo(app);
        requestRoutesNvidiaManagerEmmcSecureErase(app);
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
        requestRoutesSplitUpdateService(app);
        requestRoutesSwitchPowerMode(app);
        requestRoutesClearPCIeAerErrorStatus(app);
        requestRoutesSwitchHistogramCollection(app);
        requestRoutesSwitchHistogram(app);
        requestRoutesSwitchHistogramBucketCollection(app);
        requestRoutesSwitchHistogramBucket(app);
    }

    if constexpr (BMCWEB_NETWORK_ADAPTERS)
    {
        requestRoutesNetworkAdapters(app);
        requestRoutesNetworkDeviceFunctions(app);
        requestRoutesACDPort(app);
    }

    if constexpr (BMCWEB_HOST_ETH_IFACE)
    {
        requestHostEthernetInterfacesRoutes(app);
    }

    if constexpr (BMCWEB_NETWORK_ADAPTERS_GENERIC)
    {
        requestRoutesNetworkAdaptersGeneric(app);
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

    requestRoutesDebugToken(app);
    requestRoutesDebugTokenServiceEntry(app);
    requestRoutesDebugTokenServiceEntryCollection(app);
    requestRoutesDebugTokenServiceDiagnosticDataCollect(app);
    requestRoutesDebugTokenServiceDiagnosticDataEntryDownload(app);

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

#if defined(BMCWEB_INSECURE_ENABLE_REDFISH_FW_TFTP_UPDATE) ||                  \
    defined(BMCWEB_REDFISH_FW_SCP_UPDATE) ||                                   \
    defined(BMCWEB_REDFISH_FW_HTTP_HTTPS_UPDATE)
    requestRoutesUpdateServiceActionsSimpleUpdate(app);
#endif

    requestRoutesSoftwareInventoryCollection(app);
    requestRoutesSoftwareInventory(app);
    requestRoutesInventorySoftwareCollection(app);
    requestRoutesInventorySoftware(app);

    if constexpr (BMCWEB_MANUFACTURING_TEST)
    {
        requestRoutesEventLogDiagnosticDataCollect(app);
        requestRoutesEventLogDiagnosticDataEntry(app);
    }

    if constexpr (BMCWEB_REDFISH_DUMP_LOG)
    {
        requestRoutesSystemDumpServiceActionInfo(app);
        requestRoutesBMCDumpServiceActionInfo(app);
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
        requestRoutesBiosChangePassword(app);
        requestRoutesBootOptions(app);
        requestRoutesSecureBoot(app);
        requestRoutesSecureBootDatabase(app);
    }
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

    requestRoutesEROTChassisCertificate(app);

    requestRoutesComponentIntegrity(app);
    requestRoutesServiceConditions(app);
    requestRoutesChassisControls(app);
    requestRoutesChassisControlsCollection(app);
    requestRoutesUpdateServiceCommitImage(app);
    requestRoutesChassisControlsReset(app);
    requestRoutesTrustedComponents(app);

    if constexpr (BMCWEB_REDFISH_LEAK_DETECT)
    {
        requestRoutesLeakDetection(app);
        requestRoutesLeakDetector(app);
        requestRoutesLeakDetectionPolicy(app);
        requestPolicyCollection(app);
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

    if constexpr (BMCWEB_REDFISH_SW_EINJ)
    {
        nvidia::sweinj::requestRoutesSwEinjAction(app);
    }
}

} // namespace redfish
