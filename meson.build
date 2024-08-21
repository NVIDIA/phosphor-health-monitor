project(
    'phosphor-health-monitor',
    'cpp',
    version: '1.0',
    default_options: [
        'cpp_std=c++23',
    ],
    meson_version: '>=1.1.1',
)

phosphor_logging_dep = dependency('phosphor-logging')
phosphor_dbus_interfaces_dep = dependency('phosphor-dbus-interfaces')
sdbusplus_dep = dependency('sdbusplus')
sdeventplus_dep = dependency('sdeventplus')
nlohmann_json_dep = dependency('nlohmann_json', include_type: 'system')
base_deps = [
    phosphor_logging_dep,
    phosphor_dbus_interfaces_dep,
    sdbusplus_dep,
    sdeventplus_dep,
    nlohmann_json_dep
]

executable(
    'health-monitor',
    [
        'health_metric_config.cpp',
        'health_metric.cpp',
        'health_utils.cpp',
        'health_metric_collection.cpp',
        'health_monitor.cpp',
    ],
    dependencies: [
        base_deps
    ],
    install: true,
    install_dir: get_option('bindir')
)

executable(
    'inject-memory-leak',
    [
	'tools/memory_leak.cpp',
    ],
    install: false,
    install_dir: get_option('bindir')
)
# Include the ipc subdirectory
subdir('ipc')

log_rate_limit = get_option('log_rate_limit')
boot_delay = get_option('boot_delay')
install_data(sources : 'process_health_config.json', install_dir : '/etc/healthMon')
install_data(sources : 'system_recovery_action.sh', install_dir : get_option('bindir'), install_mode : 'rwxr-xr-x')
install_data(sources : 'run_top.sh', install_dir : get_option('bindir'), install_mode : 'rwxr-xr-x')
install_data(sources : 'system_warning_action.sh', install_dir : get_option('bindir'), install_mode : 'rwxr-xr-x')
install_data(sources : 'service_warning_action.sh', install_dir : get_option('bindir'), install_mode : 'rwxr-xr-x')
install_data(sources : 'service_recovery_action.sh', install_dir : get_option('bindir'), install_mode : 'rwxr-xr-x')

conf_data = configuration_data()
conf_data.set('HEALTH_CONFIG_FILE', '"/etc/healthMon/bmc_health_config.json"')
conf_data.set('SERVICE_HEALTH_CONFIG_FILE', '"/etc/healthMon/process_health_config.json"')
conf_data.set('HEALTH_BUS_NAME', '"xyz.openbmc_project.HealthMon"')
conf_data.set('HEALTH_SENSOR_PATH', '"/xyz/openbmc_project/sensors/utilization/"')
conf_data.set('SENSOR_OBJPATH', '"/xyz/openbmc_project/sensors"')
conf_data.set('INVENTORY_OBJPATH', '"/xyz/openbmc_project/inventory"')
conf_data.set('MONITOR_COLLECTION_INTERVAL', get_option('monitor-collection-interval'))
conf_data.set('LOG_RATE_LIMIT', log_rate_limit)
conf_data.set('BOOT_DELAY', boot_delay)
conf_data.set('ENABLE_DEBUG', false)
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
  input: 'memory_error_injection.service.in',
  output: 'memory_error_injection.service',
  configuration: conf_data,
  install: false,
  install_dir: systemd.get_variable('systemdsystemunitdir'))

  if get_option('tests').allowed()
    subdir('test')
  endif
