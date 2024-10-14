#include "redfish_nvidia.hpp"
#include "bmcweb_config.h"
#include "nvidia_cpu_debug_token.hpp"
#include "nvidia_debug_token.hpp"
#include "nvidia_error_injection.hpp"
#include "nvidia_log_services.hpp"
#include "nvidia_log_services_debug_token.hpp"
#include "nvidia_log_services_fdr.hpp"
#include "nvidia_log_services_sel.hpp"
#include "nvidia_log_services_xid.hpp"
#include "nvidia_log_services_fault.hpp"
#include "nvidia_oem_dpu.hpp"
#include "nvidia_power_smoothing.hpp"
#include "nvidia_protected_component.hpp"
#include "nvidia_workload_power_profiles.hpp"


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

    if constexpr (BMCWEB_HOST_OS_FEATURES)
    {
        requestRoutesTriggerCollection(app);
        requestRoutesTrigger(app);
    }

    if constexpr (BMCWEB_REDFISH_NEW_POWERSUBSYSTEM_THERMALSUBSYSTEM)
    {
        requestRoutesEnvironmentMetrics(app);
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

    requestRoutesEventLogServicePatch();
    requestRoutesChassisLogServiceCollection(app);

    if constexpr (BMCWEB_REDFISH_SYSTEM_FAULTLOG_DUMP_LOG)
    {
        requestRoutesSystemFaultLogService(app);
        requestRoutesSystemFaultLogEntryCollection(app);
        requestRoutesSystemFaultLogEntry(app);
        requestRoutesSystemFaultLogClear(app);
    }

#if defined(BMCWEB_INSECURE_ENABLE_REDFISH_FW_TFTP_UPDATE) ||                  \
    defined(BMCWEB_REDFISH_FW_SCP_UPDATE) ||                            \
    defined(BMCWEB_REDFISH_FW_HTTP_HTTPS_UPDATE)
    requestRoutesUpdateServiceActionsSimpleUpdate(app);
#endif

    requestRoutesSoftwareInventoryCollection(app);
    requestRoutesSoftwareInventory(app);
    requestRoutesInventorySoftwareCollection(app);
    requestRoutesInventorySoftware(app);

#ifdef BMCWEB_MFG_TEST_API
    requestRoutesEventLogDiagnosticDataCollect(app);
    requestRoutesEventLogDiagnosticDataEntry(app);
#endif

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

#ifdef BMCWEB_BIOS
    requestRoutesBiosSettings(app);
    requestRoutesBiosChangePassword(app);
    requestRoutesBootOptions(app);
    requestRoutesSecureBoot(app);
    requestRoutesSecureBootDatabase(app);
#endif
#ifdef BMCWEB_DPU_BIOS
    requestRoutesBiosAttrRegistryService(app);
#endif
#ifdef BMCWEB_HOST_IFACE
    requestHostInterfacesRoutes(app);
#endif

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

#ifdef BMCWEB_REDFISH_LEAK_DETECT
    requestRoutesLeakDetection(app);
    requestRoutesLeakDetector(app);
    requestRoutesLeakDetectionPolicy(app);
    requestPolicyCollection(app);
#endif

#ifdef BMCWEB_PROFILES
    requestRoutesProfiles(app);
#endif
}

} // namespace redfish
