# Mapping from option key name to schemas that should be installed if that option is enabled
schemas = {
    'insecure-disable-auth': 'OpenBMCAccountService',
    'redfish-oem-manager-fan-data': 'OpenBMCManager',
    'redfish-provisioning-feature': 'OpenBMCComputerSystem',
    #'vm-nbdproxy': 'OpenBMCVirtualMedia',
}

foreach option_key, schema : schemas
    if get_option(option_key).allowed()
        install_data(
            'csdl/@0@_v1.xml'.format(schema),
<<<<<<< HEAD
            install_dir: '/var/www/redfish/v1/schema',
        )

        install_data(
            'json-schema/@0@.json'.format(schema),
            install_dir: '/var/www/redfish/v1/JsonSchema/@0@'.format(schema),
=======
            install_dir: 'share/www/redfish/v1/schema',
            follow_symlinks: true,
        )

        install_data(
            'json-schema/@0@.v1_0_0.json'.format(schema),
            install_dir: 'share/www/redfish/v1/JsonSchemas',
            follow_symlinks: true,
>>>>>>> origin/master
        )
    endif
endforeach
