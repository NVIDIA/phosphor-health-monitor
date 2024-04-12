project(
    'phosphor-health-monitor',
    'cpp',
    version: '1.0',
    default_options: [
        'cpp_std=c++20',
        'werror=true',
    ],
    meson_version: '>=0.58.0',
)

cpp = meson.get_compiler('cpp')

if cpp.has_header('nlohmann/json.hpp')
    nlohmann_json = declare_dependency()
else
    subproject('nlohmann-json')
    nlohmann_json = dependency('nlohmann_json')
endif

executable(
    'health-monitor',
    [
        'healthMonitor.cpp',
    ],
    dependencies: [
        dependency('phosphor-dbus-interfaces'),
        dependency('phosphor-logging'),
        dependency('sdbusplus'),
        dependency('sdeventplus'),
        nlohmann_json,
    ],
    install: true,
    install_dir: get_option('bindir')
)

executable(
    'ipc-monitor',
    [
        'ipcMain.cpp',
        'ipcMonitor.cpp',
        'ipcHealthSensor.cpp',
        'dbusIpcSensor.cpp',
    ],
    dependencies: [
        dependency('phosphor-dbus-interfaces'),
        dependency('phosphor-logging'),
        dependency('sdbusplus'),
        dependency('sdeventplus'),
        nlohmann_json,
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
        dependency('phosphor-dbus-interfaces'),
        dependency('phosphor-logging'),
        dependency('sdbusplus'),
        dependency('sdeventplus'),
        nlohmann_json,
    ],
    install: false,
    install_dir: get_option('bindir')
)

install_data(sources : 'bmc_health_config.json', install_dir : '/etc/healthMon')
install_data(sources : 'bmc_ipc_config.json', install_dir : '/etc/healthMon')
install_data(sources : 'service_health_config.json', install_dir : '/etc/healthMon')
install_data(sources : 'system_recovery_action.sh', install_dir : get_option('bindir'), install_mode : 'rwxr-xr-x')
install_data(sources : 'run_top.sh', install_dir : get_option('bindir'), install_mode : 'rwxr-xr-x')
install_data(sources : 'system_warning_action.sh', install_dir : get_option('bindir'), install_mode : 'rwxr-xr-x')
install_data(sources : 'service_warning_action.sh', install_dir : get_option('bindir'), install_mode : 'rwxr-xr-x')
install_data(sources : 'service_recovery_action.sh', install_dir : get_option('bindir'), install_mode : 'rwxr-xr-x')
install_data(sources : 'ipc_recovery_action.sh', install_dir : get_option('bindir'), install_mode : 'rwxr-xr-x')

conf_data = configuration_data()
conf_data.set('HEALTH_CONFIG_FILE', '"/etc/healthMon/bmc_health_config.json"')
conf_data.set('IPC_CONFIG_FILE', '"/etc/healthMon/bmc_ipc_config.json"')
conf_data.set('SERVICE_HEALTH_CONFIG_FILE', '"/etc/healthMon/service_health_config.json"')
conf_data.set('HEALTH_BUS_NAME', '"xyz.openbmc_project.HealthMon"')
conf_data.set('HEALTH_SENSOR_PATH', '"/xyz/openbmc_project/sensors/utilization/"')
conf_data.set('SENSOR_OBJPATH', '"/xyz/openbmc_project/sensors"')
conf_data.set('INVENTORY_OBJPATH', '"/xyz/openbmc_project/inventory"')
conf_data.set('PRINT_HELTH_CONFIG_ENABLED', true)

configure_file(output : 'config.h',
               configuration : conf_data)

systemd = dependency('systemd')
conf_data = configuration_data()
conf_data.set('bindir', get_option('prefix') / get_option('bindir'))
configure_file(
  input: 'phosphor-health-monitor.service.in',
  output: 'phosphor-health-monitor.service',
  configuration: conf_data,
  install: true,
  install_dir: systemd.get_variable('systemdsystemunitdir'))

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
  input: 'systemrecovery.service.in',
  output: 'HMSystemRecovery@.service',
  configuration: conf_data,
  install: true,
  install_dir: systemd.get_variable('systemdsystemunitdir'))
  
  configure_file(
  input: 'systemwarning.service.in',
  output: 'HMSystemWarning@.service',
  configuration: conf_data,
  install: true,
  install_dir: systemd.get_variable('systemdsystemunitdir'))
  
  configure_file(
  input: 'servicerecovery.service.in',
  output: 'HMServiceRecovery@.service',
  configuration: conf_data,
  install: true,
  install_dir: systemd.get_variable('systemdsystemunitdir'))
  
  configure_file(
  input: 'servicewarning.service.in',
  output: 'HMServiceWarning@.service',
  configuration: conf_data,
  install: true,
  install_dir: systemd.get_variable('systemdsystemunitdir'))

configure_file(
  input: 'ipcrecovery.service.in',
  output: 'HMIpcRestart@.service',
  configuration: conf_data,
  install: true,
  install_dir: systemd.get_variable('systemdsystemunitdir'))