# OpenBMCAccountService is always installed: the AccountService payload
# unconditionally emits Oem/OpenBMC (#OpenBMCAccountService.v1_0_0.AccountService),
# so its schema must always be served (matches upstream).
install_data(
    'csdl/OpenBMCAccountService_v1.xml',
    install_dir: 'share/www/redfish/v1/schema',
    follow_symlinks: true,
)
install_data(
    'json-schema/OpenBMCAccountService.v1_0_0.json',
    install_dir: 'share/www/redfish/v1/JsonSchemas',
    follow_symlinks: true,
)

# Mapping from option key name to schemas that should be installed if that option is enabled
schemas = {
    'redfish-oem-manager-fan-data': 'OpenBMCManager',
    'redfish-provisioning-feature': 'OpenBMCComputerSystem',
    'vm-nbdproxy': 'OpenBMCVirtualMedia',
}

foreach option_key, schema : schemas
    if get_option(option_key).allowed()
        install_data(
            'csdl/@0@_v1.xml'.format(schema),
            install_dir: 'share/www/redfish/v1/schema',
            follow_symlinks: true,
        )

        install_data(
            'json-schema/@0@.v1_0_0.json'.format(schema),
            install_dir: 'share/www/redfish/v1/JsonSchemas',
            follow_symlinks: true,
        )
    endif
endforeach
