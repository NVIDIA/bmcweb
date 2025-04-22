sdbusplus = dependency('sdbusplus', fallback: ['sdbusplus', 'sdbusplus_dep'])

phosphor_logging_dep = dependency(
    'phosphor-logging',
    fallback: ['phosphor-logging', 'phosphor_logging_dep'],
)

cpp = meson.get_compiler('cpp')

if cpp.has_header('nlohmann/json.hpp')
    nlohmann_json = declare_dependency()
else
    subproject('nlohmann-json')
    nlohmann_json = declare_dependency(
        include_directories: include_directories(
            'subprojects/nlohmann-json/single_include',
            'subprojects/nlohmann-json/single_include/nlohmann',
        ),
    )
endif

inc = include_directories('include')

listener_incs = [incdir, inc]

default_deps = [nlohmann_json, sdbusplus, phosphor_logging_dep, boost]

subdir('src')
subdir('service_file')
