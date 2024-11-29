# Gather the Configuration data

conf_data = configuration_data()

if get_option('firmware-image-limit') > get_option('http-body-limit')
  error('Firmware image size limit should be lesser than http body limit.')
endif

feature_options = [
    'array-bootprogress',
    'basic-auth',
    'bios',
    'bmcweb-chunking',
    'command-shell',
    'command-smbpbi-oob',
    'cookie-auth',
    'cpu-diag-support',
    'debug-interface-support',
    'dhcp-configuration-update',
    'disable-conditions-array',
    'disable-health-rollup',
    'dot-support',
    'dpu-bios',
    'enable-fabric-adapter',
    'enable-sse',
    'enforce_sensor_name_format',
    'erot-reset',
    'experimental-http2',
    'experimental-redfish-multi-computer-system',
    'fencing-privilege',
    'force-insecure-event-notification',
    'google-api',
    'health-rollup-alternative',
    'host-auxpower-features',
    'host-eth-iface',
    'host-iface',
    'host-os-features',
    'host-serial-socket',
    'hypervisor-computer-system',
    'ibm-management-console',
    'insecure-disable-auth',
    'insecure-disable-csrf',
    'insecure-disable-ssl',
    'insecure-enable-redfish-query',
    'insecure-ignore-content-type',
    'insecure-push-style-notification',
    'insecure-tftp-update',
    'ipmi',
    'kvm',
    'lldp-dedicated-ports',
    'manual-boot-mode-support',
    'manufacturing-test',
    'meta-tls-common-name-parsing',
    'mutual-tls-auth',
    'network-adapters-generic',
    'network-adapters',
    'nic-configuration-update',
    'ntp',
    'nvidia-bootentryid',
    'nvidia-oem-bf-properties',
    'nvidia-oem-bf3-properties',
    'nvidia-oem-common-properties',
    'nvidia-oem-device-status-from-file',
    'nvidia-oem-fw-update-staging',
    'nvidia-oem-logservices',
    'nvidia-oem-gb200nvl-properties',
    'nvidia-oem-properties',
    'nvidia-uuid-from-platform-chassis-name',
    'nvidia-retimer-debugmode',
    'patch-ssh',
    'platform-metrics',
    'power-control-type-percentage',
    'profile-configuration',
    'redfish-aggregation',
    'redfish-allow-deprecated-power-thermal',
    'redfish-bmc-journal',
    'redfish-cpu-log',
    'redfish-dbus-event',
    'redfish-dbus-log',
    'redfish-dump-log',
    'redfish-enable-proccessor-memory-status',
    'redfish-fdr-log',
    'redfish-health-populate',
    'redfish-host-logger',
    'redfish-leak-detect',
    'redfish-new-powersubsystem-thermalsubsystem',
    'redfish-oem-manager-fan-data',
    'redfish-post-to-old-updateservice',
    'redfish-provisioning-feature',
    'redfish-system-faultlog-dump-log',
    'redfish-updateservice-http-pull',
    'redfish-updateservice-use-dbus',
    'redfish',
    'reset-bios-by-clear-nonvolatile',
    'rest',
    'rmedia',
    'scp-update',
    'session-auth',
    'shmem-platform-metrics',
    'static-hosting',
    'tests',
    'tls-auth-opt-in',
    'use-efficient-expand-on-sensors',
    'vm-websocket',
    'xtoken-auth',
    'nvidia-oem-openocd',
    'systems-lastresettime',
]

string_options = [
    'build-description-file-path',
    'computer-system-inventory-path',
    'dns-resolver',
    'host-iface-channel',
    'mutual-tls-common-name-parsing-default',
    'nvidia-network-adapter-link-prefix',
    'nvidia-network-adapter-prefix',
    'otp-provisioning-status-file-path',
    'platform-bmc-id',
    'platform-chassis-name',
    'platform-device-prefix',
    'platform-gpu-name-prefix',
    'platform-metrics-id',
    'platform-network-adapter',
    'platform-power-control-sensor-name',
    'platform-system-description',
    'platform-system-id',
    'platform-total-power-sensor-name',
    'redfish-aggregation-prefix',
    'redfish-manager-uri-name',
    'redfish-system-uri-name',
    'rfa-bmc-host-url',
    'rfa-hmc-update-target',
    'tls-cacert-path',
    'update-service-image-location',
    'update-service-stage-location',
]

int_options = [
    'firmware-image-limit',
    'bmcweb-response-timeout',
    'gpu-index-start',
    'http-body-limit',
    'manufacturing-test-timeout',
    'min-digits',
    'min-lcase-characters',
    'min-password-length',
    'min-special-characters',
    'min-ucase-characters',
    'platform-metrics-sensing-interval',
    'rfa-delay-subscribe-time',
    'stalesensor-upper-limit-milisecond',
    'update-service-task-timeout',
]

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

# NBD proxy is disabled due to lack of maintenance.  See meson_options.txt
feature_options_string += 'constexpr const bool        BMCWEB_VM_NBDPROXY = false;\n'

conf_data.set(
    'BMCWEB_OPTIONS',
    string_options_string + int_options_string + feature_options_string + array_options_string,
)

conf_data.set_quoted('FW_MCTP_MAPPING_JSON', join_paths(package_datadir, 'fw_mctp_mapping.json'))

conf_data.set('BMCWEB_TLS_CACERT_PATH', get_option('tls-cacert-path') )

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
               output : 'bmcweb.socket',
               install_dir: systemd_system_unit_dir,
    install: true,
    configuration: configuration_data({
        'BMCWEB_HTTPS_PORT': get_option('https_port'),
    }),
)

configure_file(
    input: 'bmcweb.service.in',
               output : 'bmcweb.service',
               install_dir: systemd_system_unit_dir,
    install: true,
    configuration: configuration_data({
        'MESON_INSTALL_PREFIX': get_option('prefix'),
    }),
)

# Copy pam-webserver to etc/pam.d
install_data(
  'pam-webserver',
  install_dir: '/etc/pam.d/',
  rename: 'webserver',
)
