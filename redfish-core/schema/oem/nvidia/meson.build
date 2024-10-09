if get_option('nvidia-oem-properties').allowed()
    # Mapping from option key name to schemas that should be installed if that option is enabled
    schemas = [
        'NvidiaChassis_v1.xml',
        'NvidiaComputerSystem_v1.xml',
        'NvidiaEnvironmentMetrics_v1.xml',
        'NvidiaErrorInjection_v1.xml',
        'NvidiaEvent_v1.xml',
        'NvidiaFirmwarePackageCollection_v1.xml',
        'NvidiaFirmwarePackage_v1.xml',
        'NvidiaLogEntry_v1.xml',
        'NvidiaLogService_v1.xml',
        'NvidiaManager_v1.xml',
        'NvidiaMemoryMetrics_v1.xml',
        'NvidiaMemory_v1.xml',
        'NvidiaMetricReport_v1.xml',
        'NvidiaNetworkAdapter_v1.xml',
        'NvidiaNVLinkMetrics_v1.xml',
        'NvidiaPCIeDevice_v1.xml',
        'NvidiaPCIeSlots_v1.xml',
        'NvidiaPersistentStorage_v1.xml',
        'NvidiaPortMetrics_v1.xml',
        'NvidiaPort_v1.xml',
        'NvidiaPowerSmoothingPresetProfileCollection_v1.xml',
        'NvidiaPowerSmoothingPresetProfile_v1.xml',
        'NvidiaPowerSmoothing_v1.xml',
        'NvidiaProcessorMetrics_v1.xml',
        'NvidiaProcessor_v1.xml',
        'NvidiaRoTChassis_v1.xml',
        'NvidiaRoTImageSlotCollection_v1.xml',
        'NvidiaRoTImageSlot_v1.xml',
        'NvidiaRoTProtectedComponentCollection_v1.xml',
        'NvidiaRoTProtectedComponent_v1.xml',
        'NvidiaSoftwareInventory_v1.xml',
        'NvidiaSwitchPowerMode_v1.xml',
        'NvidiaSwitch_v1.xml',
        'NvidiaUpdateService_v1.xml',
        'NvidiaWorkloadPowerProfileCollection_v1.xml',
        'NvidiaWorkloadPowerProfile_v1.xml',
        'NvidiaWorkloadPower_v1.xml',
    ]

    foreach schema : schemas
        install_data(
            'csdl/@0@'.format(schema),
            install_dir: '/usr/share/www/redfish/v1/schema',
        )
    endforeach
endif
