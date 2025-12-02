# Device Manager - Error Clearing Infrastructure
device_manager_sources = ['device_error_logger.cpp', 'mctp_interface.cpp']

# Dependencies
device_manager_deps = [dependency('sdbusplus'), dependency('phosphor-logging')]

# Device Manager library for error clearing
device_manager_lib = static_library(
    'device_manager',
    device_manager_sources,
    include_directories: include_directories('.'),
    dependencies: device_manager_deps,
    implicit_include_directories: false,
)

device_manager_dep = declare_dependency(
    link_with: device_manager_lib,
    include_directories: include_directories('.'),
    dependencies: device_manager_deps,
)
