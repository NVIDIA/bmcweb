listener_srcs = files('elog_entry.cpp', 'listener.cpp')
listener_deps = [default_deps, bmcweb_dependencies]

boost_srcs = files(
    '../../redfish-core/src/error_messages.cpp',
    '../../redfish-core/src/registries.cpp',
    '../../redfish-core/src/utils/json_utils.cpp',
    '../../redfish-core/src/utils/time_utils.cpp',
    '../../src/boost_asio.cpp',
    '../../src/boost_asio_ssl.cpp',
    '../../src/boost_beast.cpp',
)


executable(
    'listener',
    sources: listener_srcs,
    include_directories: listener_incs,
    dependencies: listener_deps,
    implicit_include_directories: false,
    link_args: '-Wl,--gc-sections',
    link_with: bmcweblib,
    install: true,
)
