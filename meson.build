project(
    'bmcweb',
    'cpp',
    version: '1.0',
    meson_version: '>=1.3.0',
    default_options: [
        'b_lto_mode=default',
        'b_lto_threads=0',
        'b_lto=true',
        'b_ndebug=if-release',
        'buildtype=debugoptimized',
        'cpp_rtti=true',
        'cpp_std=c++20',
        'warning_level=3',
        'werror=true',
    ],
)

# Project related links

project_pretty_name = 'bmcweb'
project_url = 'https://github.com/openbmc/' + project_pretty_name
project_issues_url = project_url + '/issues/new'
summary('Issues', project_issues_url, section: 'Report Issues')

# Validate the c++ Standard

if get_option('cpp_std') != 'c++20'
    error('This project requires c++20 support')
endif

# Get compiler and default build type

cxx = meson.get_compiler('cpp')
build = get_option('buildtype')
optimization = get_option('optimization')
summary('Build Type', build, section: 'Build Info')
summary('Optimization', optimization, section: 'Build Info')

package_datadir = join_paths(
    get_option('prefix'),
    get_option('datadir'),
    meson.project_name(),
)

# remove debug information for minsize buildtype
if (get_option('buildtype') == 'minsize')
    add_project_arguments(
        ['-fdata-sections', '-ffunction-sections'],
        language: 'cpp',
    )
    add_project_arguments('-DNDEBUG', language: 'cpp')
endif

if (get_option('dns-resolver') == 'systemd-dbus')
    add_project_arguments('-DBMCWEB_DBUS_DNS_RESOLVER', language: 'cpp')
endif

# Disable lto when compiling with no optimization
if (get_option('optimization') == '0')
    add_project_arguments('-fno-lto', language: 'cpp')
    message('Disabling lto & its supported features as optimization is disabled')
endif

# Include Directories

incdir = include_directories(
    'include',
    'redfish-core/include',
    'redfish-core/lib',
    'http',
)

# Get the options and enable the respective features
## create a MAP of  "options : feature_flag"

feature_map = {
    'lldp-dedicated-ports'                        : '-DBMCWEB_ENABLE_LLDP_DEDICATED_PORTS',
    'enforce_sensor_name_format'                  : '-DBMCWEB_ENFORCE_SENSOR_NAME_FORMAT',
    'power-control-type-percentage'               : '-DBMCWEB_CONTROL_SETPOINT_UNITS_PERCENTAGE',
    'platform-metrics'                            : '-DBMCWEB_ENABLE_PLATFORM_METRICS',
    'basic-auth'                                  : '-DBMCWEB_ENABLE_BASIC_AUTHENTICATION',
    'cookie-auth'                                 : '-DBMCWEB_ENABLE_COOKIE_AUTHENTICATION',
    'google-api'                                  : '-DBMCWEB_ENABLE_GOOGLE_API',
    'host-serial-socket'                          : '-DBMCWEB_ENABLE_HOST_SERIAL_WEBSOCKET',
    'ibm-management-console'                      : '-DBMCWEB_ENABLE_IBM_MANAGEMENT_CONSOLE',
    'insecure-disable-csrf'                       : '-DBMCWEB_INSECURE_DISABLE_CSRF_PREVENTION',
    'insecure-disable-ssl'                        : '-DBMCWEB_INSECURE_DISABLE_SSL',
    'insecure-push-style-notification'            : '-DBMCWEB_INSECURE_ENABLE_HTTP_PUSH_STYLE_EVENTING',
    'insecure-tftp-update'                        : '-DBMCWEB_INSECURE_ENABLE_REDFISH_FW_TFTP_UPDATE',
    'scp-update'                                  : '-DBMCWEB_ENABLE_REDFISH_FW_SCP_UPDATE',
    'redfish-updateservice-http-pull'             : '-DBMCWEB_ENABLE_REDFISH_FW_HTTP_HTTPS_UPDATE',
    'kvm'                                         : '-DBMCWEB_ENABLE_KVM' ,
    'mutual-tls-auth'                             : '-DBMCWEB_ENABLE_MUTUAL_TLS_AUTHENTICATION',
    'redfish-aggregation'                         : '-DBMCWEB_ENABLE_REDFISH_AGGREGATION',
    'redfish-allow-deprecated-power-thermal'      : '-DBMCWEB_ALLOW_DEPRECATED_POWER_THERMAL',
    'redfish-bmc-journal'                         : '-DBMCWEB_ENABLE_REDFISH_BMC_JOURNAL',
    'redfish-cpu-log'                             : '-DBMCWEB_ENABLE_REDFISH_CPU_LOG',
    'nvidia-oem-fw-update-staging'                : '-DBMCWEB_ENABLE_NVIDIA_UPDATE_STAGING',
    'nvidia-oem-logservices'                      : '-DBMCWEB_ENABLE_NVIDIA_OEM_LOGSERVICES',
    'nvidia-oem-common-properties'                : '-DBMCWEB_ENABLE_NVIDIA_OEM_COMMON_PROPERTIES',
    'nvidia-oem-bf-properties'                    : '-DBMCWEB_ENABLE_NVIDIA_OEM_BF_PROPERTIES',
    'nvidia-oem-bf3-properties'                   : '-DBMCWEB_ENABLE_NVIDIA_OEM_BF3_PROPERTIES',
    'nvidia-oem-gb200nvl-properties'              : '-DBMCWEB_ENABLE_NVIDIA_OEM_GB200NVL_PROPERTIES',
    'nvidia-retimer-debugmode'                    : '-DBMCWEB_ENABLE_NVIDIA_RETIMER_DEBUGMODE',
    'nvidia-oem-device-status-from-file'          : '-DBMCWEB_ENABLE_DEVICE_STATUS_FROM_FILE',
    'nvidia-device-status-from-association'       : '-DBMCWEB_ENABLE_DEVICE_STATUS_FROM_ASSOCIATION',
    'nvidia-uuid-from-platform-chassis-name'      : '-DBMCWEB_ENABLE_UUID_FROM_PLATFORM_CHASSIS_NAME',
    'network-adapters'                            : '-DBMCWEB_ENABLE_NETWORK_ADAPTERS',
    'host-eth-iface'                              : '-DBMCWEB_ENABLE_HOST_ETH_IFACE',
    'network-adapters-generic'                    : '-DBMCWEB_ENABLE_NETWORK_ADAPTERS_GENERIC',
    'redfish-dbus-log'                            : '-DBMCWEB_ENABLE_REDFISH_DBUS_LOG_ENTRIES',
    'redfish-fdr-log'                             : '-DBMCWEB_ENABLE_REDFISH_FDR_DUMP_LOG',
    'bmcweb-chunking'                             : '-DBMCWEB_ENABLE_CHUNKING',
    'redfish-host-logger'                         : '-DBMCWEB_ENABLE_REDFISH_HOST_LOGGER',
    'redfish-dbus-event'                          : '-DBMCWEB_ENABLE_REDFISH_DBUS_EVENT_PUSH',
    'redfish-new-powersubsystem-thermalsubsystem' : '-DBMCWEB_NEW_POWERSUBSYSTEM_THERMALSUBSYSTEM',
    'redfish-oem-manager-fan-data'                : '-DBMCWEB_ENABLE_REDFISH_OEM_MANAGER_FAN_DATA',
    'redfish-post-to-old-updateservice'           : '-DBMCWEB_ENABLE_REDFISH_UPDATESERVICE_OLD_POST_URL',
    'redfish'                                     : '-DBMCWEB_ENABLE_REDFISH',
    'rest'                                        : '-DBMCWEB_ENABLE_DBUS_REST',
    'session-auth'                                : '-DBMCWEB_ENABLE_SESSION_AUTHENTICATION',
    'static-hosting'                              : '-DBMCWEB_ENABLE_STATIC_HOSTING',
    'experimental-redfish-multi-computer-system'  : '-DBMCWEB_ENABLE_MULTI_COMPUTERSYSTEM',
    'vm-websocket'                                : '-DBMCWEB_ENABLE_VM_WEBSOCKET',
    'xtoken-auth'                                 : '-DBMCWEB_ENABLE_XTOKEN_AUTHENTICATION',
    'health-rollup-alternative'                   : '-DBMCWEB_ENABLE_HEALTH_ROLLUP_ALTERNATIVE',
    'disable-health-rollup'                       : '-DBMCWEB_DISABLE_HEALTH_ROLLUP',
    'disable-conditions-array'                    : '-DBMCWEB_DISABLE_CONDITIONS_ARRAY',
    'rmedia'                	                    : '-DBMCWEB_ENABLE_RMEDIA',
    'tls-auth-opt-in'                             : '-DBMCWEB_ENABLE_TLS_AUTH_OPT_IN',
    'manufacturing-test'                          : '-DBMCWEB_ENABLE_MFG_TEST_API',
    'host-iface'                                	: '-DBMCWEB_ENABLE_HOST_IFACE',
    'array-bootprogress'                          : '-DBMCWEB_ARRAY_BOOT_PROGRESS',
    'bios'                                        : '-DBMCWEB_ENABLE_BIOS',
    'dpu-bios'                                    : '-DBMCWEB_ENABLE_DPU_BIOS',
    'ipmi'                                        : '-DBMCWEB_ENABLE_IPMI',
    'ntp'                                         : '-DBMCWEB_ENABLE_NTP',
    'patch-ssh'                                   : '-DBMCWEB_ENABLE_PATCH_SSH',
    'dhcp-configuration-update'                   : '-DBMCWEB_DHCP_CONFIGURATION_UPDATE',
    'host-os-features'                            : '-DBMCWEB_ENABLE_HOST_OS_FEATURE',
    'hide-host-os-features-init-value'            : '-DBMCWEB_HIDE_HOST_OS_INIT_VALUE',
    'fencing-privilege'                           : '-DBMCWEB_ENABLE_FENCING_PRIVILEGE',
    'host-auxpower-features'                            : '-DBMCWEB_ENABLE_HOST_AUX_POWER',
    'nic-configuration-update'                    : '-DBMCWEB_NIC_CONFIGURATION_UPDATE',
    'command-shell'                               : '-DBMCWEB_ENABLE_IPMI_SOL',
    'redfish-system-faultlog-dump-log'            : '-DBMCWEB_ENABLE_REDFISH_SYSTEM_FAULTLOG_DUMP_LOG',
    'reset-bios-by-clear-nonvolatile'             : '-DBMCWEB_RESET_BIOS_BY_CLEAR_NONVOLATILE',
    'dot-support'                                 : '-DBMCWEB_ENABLE_DOT',
    'manual-boot-mode-support'                    : '-DBMCWEB_ENABLE_MANUAL_BOOT_MODE',
    'force-insecure-event-notification'           : '-DBMCWEB_FORCE_INSECURE_EVENT_NOTIFICATION',
    'use-efficient-expand-on-sensors'             : '-DBMCWEB_ENABLE_EFFICIENT_EXPAND',
    'shmem-platform-metrics'                      : '-DBMCWEB_ENABLE_SHMEM_PLATFORM_METRICS',
    'enable-sse'                                  : '-DBMCWEB_ENABLE_SSE',
    'erot-reset'                                  : '-DBMCWEB_ENABLE_EROT_RESET',
    'cpu-diag-support'                            : '-DBMCWEB_ENABLE_CPU_DIAG_FEATURE',
    'command-smbpbi-oob'                          : '-DBMCWEB_COMMAND_SMBPBI_OOB',
    'redfish-leak-detect'                         : '-DBMCWEB_ENABLE_REDFISH_LEAK_DETECT',
    'profile-configuration'                       : '-DBMCWEB_ENABLE_PROFILES',
}

if (get_option('tls-auth-opt-in').enabled())
    if (get_option('insecure-disable-auth').enabled())
        #error('Feature tls-auth-opt-in conflicts with insecure-disable-auth, only one can be enabled!')
    endif
    if (get_option('insecure-disable-ssl').enabled())
        #error('Feature tls-auth-opt-in conflicts with insecure-disable-ssl, only one can be enabled!')
    endif
endif



# Get the options status and build a project summary to show which flags are
# being enabled during the configuration time.

foreach option_key, option_value : feature_map
    if (get_option(option_key).allowed())
        if (option_key == 'mutual-tls-auth' or option_key == 'insecure-disable-ssl')
            if (get_option('insecure-disable-ssl').disabled() or get_option(
                'mutual-tls-auth',
            ).disabled())
                add_project_arguments(option_value, language: 'cpp')
                summary(option_key, option_value, section: 'Enabled Features')
            endif
        elif (option_key in [
            'basic-auth',
            'cookie-auth',
            'session-auth',
            'xtoken-auth',
            'mutual-tls-auth',
        ])
            if (get_option('insecure-disable-auth').disabled())
                add_project_arguments(option_value, language: 'cpp')
                summary(option_key, option_value, section: 'Enabled Features')
            endif
        else
            summary(option_key, option_value, section: 'Enabled Features')
            add_project_arguments(option_value, language: 'cpp')
        endif
    else
        if (option_key == 'insecure-disable-ssl')
            summary('ssl', '-DBMCWEB_ENABLE_SSL', section: 'Enabled Features')
            add_project_arguments('-DBMCWEB_ENABLE_SSL', language: 'cpp')
        endif
    endif
endforeach

if (get_option('tests').allowed())
    summary('unittest', 'NA', section: 'Features')
endif

# Add compiler arguments

if (cxx.get_id() == 'clang')
    if (cxx.version().version_compare('<17.0'))
        error('This project requires clang-17 or higher')
    endif
    add_project_arguments(
        '-Weverything',
        '-Wformat=2',
        '-Wno-c++98-compat-pedantic',
        '-Wno-c++98-compat',
        '-Wno-c++20-extensions',
        '-Wno-documentation-unknown-command',
        '-Wno-documentation',
        '-Wno-exit-time-destructors',
        '-Wno-global-constructors',
        '-Wno-newline-eof',
        '-Wno-padded',
        '-Wno-shadow',
        '-Wno-used-but-marked-unused',
        '-Wno-weak-vtables',
        '-Wno-switch-enum',
        '-Wno-unused-macros',
        '-Wno-covered-switch-default',
        '-Wno-error=deprecated-declarations',
        '-Wno-attributes',
        '-Wno-comment',
        '-Wno-disabled-macro-expansion',
        language: 'cpp',
    )
endif

if (cxx.get_id() == 'gcc')
    if (cxx.version().version_compare('<13.0'))
        error('This project requires gcc-13 or higher')
    endif

    add_project_arguments(
        '-Wformat=2',
        '-Wcast-align',
        '-Wconversion',
        '-Woverloaded-virtual',
        '-Wsign-conversion',
        '-Wunused',
        '-Wduplicated-cond',
        '-Wduplicated-branches',
        '-Wlogical-op',
        '-Wnull-dereference',
        '-Wunused-parameter',
        '-Wdouble-promotion',
        #'-Wshadow',
        #'-Wno-psabi',
        '-Wno-attributes',
        '-Wno-error=deprecated-declarations',
        '-Wno-comment',
        language: 'cpp',
    )
endif

if (get_option('buildtype') != 'plain')
    if (get_option('b_lto') == true and get_option('optimization') != '0')
        # Reduce the binary size by removing unnecessary
        # dynamic symbol table entries

        add_project_arguments(
            cxx.get_supported_arguments(
                [
                    '-fno-fat-lto-objects',
                    '-fvisibility=hidden',
                    '-fvisibility-inlines-hidden',
                ],
            ),
            language: 'cpp',
        )

        if cxx.has_link_argument('-Wl,--exclude-libs,ALL')
            add_project_link_arguments('-Wl,--exclude-libs,ALL', language: 'cpp')
        endif
    endif
endif
# Set Compiler Security flags

security_flags = [
    '-fstack-protector-strong',
    '-fPIE',
    '-fPIC',
    '-D_FORTIFY_SOURCE=2',
    '-Wformat',
    '-Wformat-security',
]

## Add security flags for builds of type 'release','debugoptimized' and 'minsize'

if not (get_option('buildtype') == 'plain'
or get_option('buildtype').startswith('debug')
)
    add_project_arguments(
        cxx.get_supported_arguments([security_flags]),
        language: 'cpp',
    )
endif

# Boost dependency configuration

add_project_arguments(
    cxx.get_supported_arguments(
        [
            '-DBOOST_ASIO_DISABLE_CONCEPTS',
            '-DBOOST_ALL_NO_LIB',
            '-DBOOST_ALLOW_DEPRECATED_HEADERS',
            '-DBOOST_ASIO_DISABLE_THREADS',
            '-DBOOST_ASIO_NO_DEPRECATED',
            '-DBOOST_ASIO_SEPARATE_COMPILATION',
            '-DBOOST_BEAST_SEPARATE_COMPILATION',
            '-DBOOST_EXCEPTION_DISABLE',
            '-DBOOST_NO_EXCEPTIONS',
            '-DBOOST_URL_NO_SOURCE_LOCATION',
            '-DBOOST_SPIRIT_X3_NO_RTTI',
            '-DJSON_NOEXCEPTION',
            '-DOPENSSL_NO_FILENAMES',
            '-DSDBUSPLUS_DISABLE_BOOST_COROUTINES',
        ],
    ),
    language: 'cpp',
)

# Find the dependency modules, if not found use meson wrap to get them
# automatically during the configure step
bmcweb_dependencies = []

pam = cxx.find_library('pam', required: true)
atomic = cxx.find_library('atomic', required: true)
bmcweb_dependencies += [pam, atomic]

openssl = dependency('openssl', required: false, version: '>=3.0.0')
if not openssl.found() or get_option('b_sanitize') != 'none'
    openssl_proj = subproject(
        'openssl',
        required: true,
        default_options: ['warning_level=0', 'werror=false'],
    )
    openssl = openssl_proj.get_variable('openssl_dep')
    openssl = openssl.as_system('system')
endif
bmcweb_dependencies += [openssl]

if get_option('shmem-platform-metrics').enabled()
    nvtal = dependency('nvidia-tal', required: true)
    bmcweb_dependencies += nvtal
endif

nghttp2 = dependency('libnghttp2', version: '>=1.52.0', required: false)
if not nghttp2.found()
    cmake = import('cmake')
    opt_var = cmake.subproject_options()
    opt_var.add_cmake_defines(
        {'ENABLE_LIB_ONLY': true, 'ENABLE_STATIC_LIB': true},
    )
    nghttp2_ex = cmake.subproject('nghttp2', options: opt_var)
    nghttp2 = nghttp2_ex.dependency('nghttp2')
endif
bmcweb_dependencies += nghttp2

sdbusplus = dependency('sdbusplus', required: false, include_type: 'system')
if not sdbusplus.found()
    sdbusplus_proj = subproject('sdbusplus', required: true)
    sdbusplus = sdbusplus_proj.get_variable('sdbusplus_dep')
    sdbusplus = sdbusplus.as_system('system')
endif
bmcweb_dependencies += sdbusplus

tinyxml = dependency(
    'tinyxml2',
    include_type: 'system',
    version: '>=9.0.0',
    default_options: ['tests=false'],
)
if not tinyxml.found()
    tinyxml_proj = subproject('tinyxml2', required: true)
    tinyxml = tinyxml_proj.get_variable('tinyxml_dep')
    tinyxml = tinyxml.as_system('system')
endif
bmcweb_dependencies += tinyxml

systemd = dependency('systemd')
add_project_arguments('-DSYSTEMD_VERSION=' + systemd.version(), language: 'cpp')

zlib = dependency('zlib')
bmcweb_dependencies += [systemd, zlib]

pwquality_dep = dependency('pwquality', required: false)
if pwquality_dep.found()
    add_project_arguments('-DHAVE_PWQUALITY', language: 'cpp')
endif
bmcweb_dependencies += pwquality_dep

nlohmann_json_dep = dependency(
    'nlohmann_json',
    version: '>=3.11.2',
    include_type: 'system',
)
bmcweb_dependencies += nlohmann_json_dep

boost = dependency(
    'boost',
    modules: ['url'],
    version: '>=1.84.0',
    required: false,
    include_type: 'system',
)
if boost.found()
    bmcweb_dependencies += [boost]
else
    cmake = import('cmake')
    opt = cmake.subproject_options()
    boost_libs = [
        'asio',
        'beast',
        'callable_traits',
        'circular_buffer',
        'format',
        'headers',
        'interprocess',
        'process',
        'type_index',
        'url',
        'uuid',
        'spirit',
    ]
    opt.add_cmake_defines(
        {
            'BOOST_INCLUDE_LIBRARIES': ';'.join(boost_libs),
            'BUILD_SHARED_LIBS': 'OFF',
        },
    )

    boost = cmake.subproject('boost', required: true, options: opt)
    foreach boost_lib : boost_libs
        boost_lib_instance = boost.dependency('boost_' + boost_lib).as_system()
        bmcweb_dependencies += [boost_lib_instance]
    endforeach
endif

if get_option('tests').allowed()
    gtest = dependency(
        'gtest',
        main: true,
        version: '>=1.14.0',
        disabler: true,
        required: false,
    )
    gmock = dependency('gmock', required: false)
    if not gtest.found() and get_option('tests').allowed()
        gtest_proj = subproject('gtest', required: true)
        gtest = gtest_proj.get_variable('gtest_main_dep')
        gmock = gtest_proj.get_variable('gmock_dep')
    endif
    gtest = gtest.as_system('system')
    gmock = gmock.as_system('system')
endif

systemd_system_unit_dir = systemd.get_variable('systemdsystemunitdir')

bindir = get_option('prefix') + '/' + get_option('bindir')

summary(
    {
        'prefix' : get_option('prefix'),
        'bindir' : bindir,
        'systemd unit directory': systemd_system_unit_dir,
    },
    section: 'Directories',
)

subdir('static')
subdir('redfish-core')

# Config subdirectory
subdir('config')
bmcweb_dependencies += conf_h_dep

# Source files
fs = import('fs')

srcfiles_bmcweb = files(
# end large files

    'redfish-core/src/error_messages.cpp',
    # Begin large files, should be at the beginning
    'redfish-core/src/filter_expr_executor.cpp',
    'redfish-core/src/filter_expr_printer.cpp',
    'redfish-core/src/redfish.cpp',
    'redfish-core/src/registries.cpp',
    'redfish-core/src/utils/dbus_utils.cpp',
    'redfish-core/src/utils/json_utils.cpp',
    'redfish-core/src/utils/time_utils.cpp',
    'redfish-core/src/utils/nsm_cmd_support.cpp',
    'src/boost_asio.cpp',
    'src/boost_asio_ssl.cpp',
    'src/boost_beast.cpp',
    'src/dbus_singleton.cpp',
    'src/json_html_serializer.cpp',
    'src/ossl_random.cpp',
    'src/webserver_run.cpp',
)

bmcweblib = static_library(
    'bmcweblib',
    srcfiles_bmcweb,
    include_directories: incdir,
    dependencies: bmcweb_dependencies,
)

# Generate the bmcweb executable
executable(
    'bmcweb',
    'src/webserver_main.cpp',
    include_directories: incdir,
    dependencies: bmcweb_dependencies,
    link_with: bmcweblib,
    link_args: '-Wl,--gc-sections',
    install: true,
    install_dir: bindir,
)

srcfiles_unittest = files(
    'test/http/crow_getroutes_test.cpp',
    'test/http/http2_connection_test.cpp',
    'test/http/http_body_test.cpp',
    'test/http/http_connection_test.cpp',
    'test/http/http_response_test.cpp',
    'test/http/mutual_tls.cpp',
    'test/http/mutual_tls_meta.cpp',
    'test/http/parsing_test.cpp',
    'test/http/router_test.cpp',
    'test/http/server_sent_event_test.cpp',
    'test/http/utility_test.cpp',
    'test/http/verb_test.cpp',
    'test/include/async_resolve_test.cpp',
    'test/include/credential_pipe_test.cpp',
    'test/include/dbus_utility_test.cpp',
    'test/include/google/google_service_root_test.cpp',
    'test/include/http_utility_test.cpp',
    'test/include/human_sort_test.cpp',
    'test/include/ibm/configfile_test.cpp',
    'test/include/json_html_serializer.cpp',
    'test/include/multipart_test.cpp',
    'test/include/openbmc_dbus_rest_test.cpp',
    'test/include/ossl_random.cpp',
    #'test/include/str_utility_test.cpp',
    'test/redfish-core/include/event_service_manager_test.cpp',
    'test/redfish-core/include/filter_expr_executor_test.cpp',
    'test/redfish-core/include/filter_expr_parser_test.cpp',
    'test/redfish-core/include/privileges_test.cpp',
    'test/redfish-core/include/redfish_aggregator_test.cpp',
    'test/redfish-core/include/registries_test.cpp',
    'test/redfish-core/include/utils/dbus_utils.cpp',
    #'test/redfish-core/include/utils/hex_utils_test.cpp',
    'test/redfish-core/include/utils/ip_utils_test.cpp',
    'test/redfish-core/include/utils/json_utils_test.cpp',
    'test/redfish-core/include/utils/query_param_test.cpp',
    #'test/redfish-core/include/utils/stl_utils_test.cpp',
    'test/redfish-core/include/utils/time_utils_test.cpp',
    #'test/redfish-core/lib/chassis_test.cpp',
    #'test/redfish-core/lib/log_services_dump_test.cpp',
    'test/redfish-core/lib/log_services_test.cpp',
    'test/redfish-core/lib/manager_diagnostic_data_test.cpp',
    'test/redfish-core/lib/power_subsystem_test.cpp',
    'test/redfish-core/lib/sensors_test.cpp',
    'test/redfish-core/lib/service_root_test.cpp',
    'test/redfish-core/lib/system_test.cpp',
    'test/redfish-core/lib/thermal_subsystem_test.cpp',
    #'test/redfish-core/lib/update_service_test.cpp',
)

if (get_option('tests').allowed())
    # generate the test executable
    ut_bin = executable(
        'bmcweb_unit_test',
        srcfiles_unittest + srcfiles_bmcweb,
        include_directories: incdir,
        install_dir: bindir,
        dependencies: bmcweb_dependencies + [gtest, gmock],
    )
    test('bmcweb_unit_test', ut_bin)
    foreach test_src : srcfiles_unittest
        test_bin = executable(
            fs.stem(test_src),
            test_src,
            link_with: bmcweblib,
            include_directories: incdir,
            install_dir: bindir,
            dependencies: bmcweb_dependencies + [gtest, gmock],
        )
        test(fs.stem(test_src), test_bin)
    endforeach
endif

if (get_option('redfish-aggregation').enabled())
    subdir('event-listener')
endif
