# Gather the Configuration data

conf_data = configuration_data()

if get_option('firmware-image-limit') > get_option('http-body-limit')
    error('Firmware image size limit should be lesser than http body limit.')
else
    conf_data.set('FIRMWARE_IMAGE_LIMIT_MB', get_option('firmware-image-limit'))
endif
conf_data.set(
    'STALE_SENSOR_UPPER_LIMIT_IN_MILISECOND',
    get_option('stalesensor-upper-limit-milisecond'),
)
conf_data.set(
    'PLATFORM_METRICS_SENSING_INTERVAL',
    get_option('platform-metrics-sensing-interval'),
)
conf_data.set(
    'UPDATE_SERVICE_IMAGE_LOCATION',
    get_option('update-service-image-location'),
)
conf_data.set(
    'UPDATE_SERVICE_STAGE_LOCATION',
    get_option('update-service-stage-location'),
)
conf_data.set(
    'UPDATE_SERVICE_TASK_TIMEOUT',
    get_option('update-service-task-timeout'),
)
conf_data.set('MFG_TEST_TIMEOUT', get_option('manufacturing-test-timeout'))
conf_data.set_quoted('PLATFORM_SYSTEM_ID', get_option('platform-system-id'))
conf_data.set_quoted(
    'PLATFORM_SYSTEM_DESCRIPTION',
    get_option('platform-system-description'),
)
conf_data.set_quoted(
    'PLATFORM_DEVICE_PREFIX',
    get_option('platform-device-prefix'),
)
conf_data.set_quoted('PLATFORM_BMC_ID', get_option('platform-bmc-id'))
conf_data.set_quoted(
    'PLATFORM_NETWORK_ADAPTER',
    get_option('platform-network-adapter'),
)
conf_data.set_quoted(
    'PLATFORM_TOTAL_POWER_SENSOR_NAME',
    get_option('platform-total-power-sensor-name'),
)
conf_data.set_quoted(
    'PLATFORM_POWER_CONTOL_SENSOR_NAME',
    get_option('platform-power-control-sensor-name'),
)
conf_data.set_quoted('PLATFORM_METRICS_ID', get_option('platform-metrics-id'))
conf_data.set_quoted('HOST_IFACE_CHANNEL', get_option('host-iface-channel'))
conf_data.set_quoted(
    'PLATFORM_GPU_NAME_PREFIX',
    get_option('platform-gpu-name-prefix'),
)
conf_data.set_quoted(
    'PLATFORM_CHASSIS_NAME',
    get_option('platform-chassis-name'),
)
conf_data.set_quoted(
    'RFA_HMC_UPDATE_TARGET',
    get_option('rfa-hmc-update-target'),
)
conf_data.set_quoted('RFA_BMC_HOST_URL', get_option('rfa-bmc-host-url'))
conf_data.set_quoted(
    'NETWORK_ADAPTER_PREFIX',
    get_option('nvidia-network-adapter-prefix'),
)
conf_data.set_quoted(
    'NETWORK_ADAPTER_LINK_PREFIX',
    get_option('nvidia-network-adapter-link-prefix'),
)
conf_data.set('RFA_SUBSCRIBE_TIME', get_option('rfa-delay-subscribe-time'))
conf_data.set_quoted(
    'REDFISH_AGGREGATION_PREFIX',
    get_option('redfish-aggregation-prefix'),
)
conf_data.set_quoted(
    'FW_UUID_MAPPING_JSON',
    join_paths(package_datadir, 'fw_uuid_mapping.json'),
)
conf_data.set(
    'BUILD_DESCRIPTION_FILE_PATH',
    get_option('build-description-file-path'),
)
conf_data.set(
    'COMPUTER_SYSTEM_INVENTORY_PATH',
    get_option('computer-system-inventory-path'),
)
conf_data.set(
    'OTP_PROVISIONING_STATUS_FILE_PATH',
    get_option('otp-provisioning-status-file-path'),
)
conf_data.set('BMCWEB_LOG_LEVEL', get_option('bmcweb-logging-log-level'))

conf_data.set('GPU_INDEX_START', get_option('gpu-index-start'))
enable_health_populate = get_option('redfish-health-populate')
conf_data.set10(
    'BMCWEB_ENABLE_HEALTH_POPULATE',
    enable_health_populate.enabled(),
)
enable_proc_mem_status = get_option('redfish-enable-proccessor-memory-status')
conf_data.set10(
    'BMCWEB_ENABLE_PROC_MEM_STATUS',
    enable_proc_mem_status.enabled(),
)
conf_data.set('BMCWEB_RESPONSE_TIMEOUT', get_option('bmcweb-response-timeout'))
nvidia_bootentryid = get_option('nvidia-bootentryid')
conf_data.set10('BMCWEB_ENABLE_NVIDIA_BOOTENTRYID', nvidia_bootentryid.enabled())



feature_options = [
    'basic-auth',
    'check-oem-diagnostic-type',
    'cookie-auth',
    'enable-debug-interface',
    'experimental-http2',
    'experimental-redfish-multi-computer-system',
    'google-api',
    'host-serial-socket',
    'ibm-management-console',
    'insecure-disable-auth',
    'insecure-disable-csrf',
    'insecure-disable-ssl',
    'insecure-enable-redfish-query',
    'insecure-ignore-content-type',
    'insecure-push-style-notification',
    'insecure-tftp-update',
    'kvm',
    'meta-tls-common-name-parsing',
    'mutual-tls-auth',
    'nsm-raw-command-enable',
    'nvidia-oem-openocd',
    'redfish-aggregation',
    'redfish-allow-deprecated-power-thermal',
    'redfish-bmc-journal',
    'redfish-cpu-log',
    'redfish-dbus-log',
    'redfish-dump-log',
    'redfish-host-logger',
    'redfish-new-powersubsystem-thermalsubsystem',
    'redfish-oem-manager-fan-data',
    'redfish-provisioning-feature',
    'redfish-updateservice-use-dbus',
    'redfish',
    'rest',
    'session-auth',
    'nvidia-oem-properties',
    'static-hosting',
    'systems-lastresettime',
    'tests',
    'vm-websocket',
    'vm-nbdproxy',
    'xtoken-auth',
    'sort-event-log',
]

string_options = [
    'dns-resolver',
    'mutual-tls-common-name-parsing-default',
    'redfish-manager-uri-name',
    'redfish-system-uri-name',
]

int_options = ['http-body-limit']

array_options = ['oem-diagnostic-allowable-type']

feature_options_string = '\n//Feature options\n'
string_options_string = '\n// String options\n'
int_options_string = '\n// Integer options\n'
array_options_string = '\n// Array options\n'

foreach option_key : feature_options + string_options + int_options + array_options
    option_key_config = 'BMCWEB_' + option_key.to_upper()
    option_key_config = option_key_config.replace('-', '_')

    message(option_key_config)

    opt = get_option(option_key)
    if string_options.contains(option_key)
        string_options_string += 'constexpr std::string_view  ' + option_key_config + ' = "' + opt + '";\n'
    elif int_options.contains(option_key)
        int_options_string += 'constexpr const int         ' + option_key_config + ' = ' + opt.to_string() + ';\n'
    elif array_options.contains(option_key)
        array_options_string += 'constexpr std::array<std::string_view, '
        array_options_string += opt.length().to_string() + '> ' + option_key_config
        array_options_string += ' = {\n    "'
        array_options_string += '",\n    "'.join(opt)
        array_options_string += '"\n};\n'
    else
        feature_options_string += 'constexpr const bool        ' + option_key_config + ' = ' + opt.allowed().to_string() + ';\n'
        opt = opt.allowed().to_string()
    endif
    summary(option_key, opt, section: 'Features')
endforeach


conf_data.set('MESON_INSTALL_PREFIX', get_option('prefix'))

# Logging level
loglvlopt = get_option('bmcweb-logging')
if get_option('buildtype').startswith('debug') and loglvlopt == 'disabled'
    # Override logging level as 'debug' if 'bmcweb-logging' is set as 'disabled'
    loglvlopt = 'debug'
endif
loglvlopt = loglvlopt.to_upper()
string_options_string += 'constexpr std::string_view  BMCWEB_LOGGING_LEVEL' + ' = "' + loglvlopt + '";\n'

conf_data.set(
    'BMCWEB_OPTIONS',
    string_options_string + int_options_string + feature_options_string + array_options_string,
)

conf_data.set('TLS_CACERT_PATH', get_option('tls-cacert-path'))

conf_h_dep = declare_dependency(
    include_directories: include_directories('.'),
    sources: configure_file(
        input: 'bmcweb_config.h.in',
        output: 'bmcweb_config.h',
        configuration: conf_data,
    ),
)

# Configure and install systemd unit files
configure_file(
    input: 'bmcweb.socket.in',
    output: 'bmcweb.socket',
    install_dir: systemd_system_unit_dir,
    install: true,
    configuration: configuration_data(
        {'BMCWEB_HTTPS_PORT': get_option('https_port')},
    ),
)

configure_file(
    input: 'bmcweb.service.in',
    output: 'bmcweb.service',
    install_dir: systemd_system_unit_dir,
    install: true,
    configuration: configuration_data(
        {'MESON_INSTALL_PREFIX': get_option('prefix')},
    ),
)

# Copy pam-webserver to etc/pam.d
install_data('pam-webserver', install_dir: '/etc/pam.d/', rename: 'webserver')
