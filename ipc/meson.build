executable(
    'ipc-monitor',
    [
        'ipcMain.cpp',
        'ipcMonitor.cpp',
        'ipcHealthSensor.cpp',
        'dbusIpcSensor.cpp',
    ],
    dependencies: [
        base_deps
    ],
    install: true,
    install_dir: get_option('bindir')
)

executable(
    'inject-error',
    [
        'tools/dbus_error_injection.cpp',
    ],
    dependencies: [
        base_deps
    ],
    install: false,
    install_dir: get_option('bindir')
)
install_data(sources : 'bmc_ipc_config.json', install_dir : '/etc/healthMon')
install_data(sources : 'ipc_recovery_action.sh', install_dir : get_option('bindir'), install_mode : 'rwxr-xr-x')

conf_data = configuration_data()
conf_data.set('IPC_CONFIG_FILE', '"/etc/healthMon/bmc_ipc_config.json"')

configure_file(output : 'config.h',
               configuration : conf_data)

systemd = dependency('systemd')
conf_data = configuration_data()
conf_data.set('bindir', get_option('prefix') / get_option('bindir'))

configure_file(
  input: 'phosphor-ipc-monitor.service.in',
  output: 'phosphor-ipc-monitor.service',
  configuration: conf_data,
  install: true,
  install_dir: systemd.get_variable('systemdsystemunitdir'))

configure_file(
  input: 'ipc_error_injuction_test.service.in',
  output: 'ipc_error_injuction_test.service',
  configuration: conf_data,
  install: false,
  install_dir: systemd.get_variable('systemdsystemunitdir'))

  configure_file(
  input: 'ipcrecovery.service.in',
  output: 'HMIpcRestart@.service',
  configuration: conf_data,
  install: true,
  install_dir: systemd.get_variable('systemdsystemunitdir'))