#include "device_manager.hpp"

#include "device_error_logger.hpp"

#include <phosphor-logging/lg2.hpp>

namespace phosphor::device::manager
{

// Global device registry - EID -> DeviceNode pointer
std::map<uint8_t, std::unique_ptr<DeviceNode>> devicesByEID;

DeviceManager::DeviceManager(sdbusplus::async::context& ctx) : ctx(ctx)
{
    lg2::info("Initializing Device Manager");

    try
    {
        // Step 1: Initialize EntityManager interface
        emInterface = std::make_unique<EntityManagerInterface>(ctx.get_bus());

        // Step 2: Query existing devices and populate global registry
        initializeDeviceRegistry();

        // Step 3: Initialize USB hotplug monitoring
        try
        {
            usbHotplugMonitor = std::make_unique<USBHotplugMonitor>(ctx);
        }
        catch (const std::exception& e)
        {
            lg2::warning("Failed to initialize USB hotplug monitor: {ERROR}",
                         "ERROR", e.what());
            // Continue without hotplug monitoring
        }

        // Step 4: Register devices for hotplug monitoring
        registerDevicesForHotplugMonitoring();

        // Step 5: Start USB hotplug event processing
        if (usbHotplugMonitor)
        {
            usbHotplugMonitor->startEventProcessing();
        }

        // Step 7: Setup monitoring for new devices from EntityManager
        emInterface->setupSignalMonitoring(
            [this](const PropertyMap& deviceProperties) {
                onEntityManagerDeviceAdded(deviceProperties);
            });

        // Step 8: Initialize MCTP endpoint monitoring
        mctpInterface = std::make_unique<MCTPInterface>(ctx.get_bus());

        // Step 9: Initialize power state monitoring
        powerStateMonitor =
            std::make_unique<PowerStateMonitor>(ctx.get_bus(), devicesByEID);

        lg2::info("Device Manager initialized successfully");
    }
    catch (const std::exception& e)
    {
        lg2::error("Failed to initialize Device Manager: {ERROR}", "ERROR",
                   e.what());
        throw;
    }
}

void DeviceManager::initializeDeviceRegistry()
{
    try
    {
        auto devices = emInterface->queryAllDevices();
        lg2::info("Found {COUNT} existing devices from EntityManager", "COUNT",
                  devices.size());

        for (const auto& deviceProperties : devices)
        {
            try
            {
                auto device = std::make_unique<DeviceNode>(deviceProperties);

                // Store device in global registry if it has an EID
                if (device->eid)
                {
                    uint8_t eid = *device->eid;
                    devicesByEID[eid] = std::move(device);
                    lg2::info("Device registered: {NAME} -> EID {EID}", "NAME",
                              devicesByEID[eid]->name, "EID", eid);
                }
                else
                {
                    lg2::debug(
                        "Device {NAME} has no EID, skipping registration",
                        "NAME", device->name);
                }
            }
            catch (const std::exception& e)
            {
                lg2::error("Failed to process existing device: {ERROR}",
                           "ERROR", e.what());
            }
        }

        lg2::info("Device registry initialized with {COUNT} devices", "COUNT",
                  devicesByEID.size());
    }
    catch (const std::exception& e)
    {
        lg2::warning(
            "Failed to query existing devices from EntityManager: {ERROR}",
            "ERROR", e.what());
    }
}

void DeviceManager::registerDevicesForHotplugMonitoring()
{
    lg2::info("Registering {COUNT} devices for hotplug monitoring", "COUNT",
              devicesByEID.size());

    if (!usbHotplugMonitor)
    {
        lg2::warning(
            "USB hotplug monitor not available, skipping device registration");
        return;
    }

    for (const auto& [eid, device] : devicesByEID)
    {
        lg2::debug("Registering device for hotplug: {NAME} (EID {EID})", "NAME",
                   device->name, "EID", eid);
        usbHotplugMonitor->registerDevice(*device);
    }

    lg2::info("Device hotplug registration completed");
}

void DeviceManager::onEntityManagerDeviceAdded(const PropertyMap& properties)
{
    try
    {
        auto device = std::make_unique<DeviceNode>(properties);

        lg2::info("Processing new device from EM: {NAME}, EID: {EID}", "NAME",
                  device->name, "EID",
                  device->eid ? std::to_string(*device->eid) : "none");

        // Store device in global registry if it has an EID
        if (device->eid)
        {
            uint8_t eid = *device->eid;

            // Register for USB hotplug monitoring
            if (usbHotplugMonitor)
            {
                usbHotplugMonitor->registerDevice(*device);
            }

            // Store in registry
            devicesByEID[eid] = std::move(device);
            lg2::info("New device registered: {NAME} -> EID {EID}", "NAME",
                      devicesByEID[eid]->name, "EID", eid);
        }
        else
        {
            lg2::debug("New device {NAME} has no EID, skipping registration",
                       "NAME", device->name);
        }
    }
    catch (const std::exception& e)
    {
        lg2::error("Failed to process new device from EM signal: {ERROR}",
                   "ERROR", e.what());
    }
}

} // namespace phosphor::device::manager
