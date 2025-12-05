#include "device_manager.hpp"

#include "device_error_logger.hpp"

#include <phosphor-logging/lg2.hpp>

namespace phosphor::device::manager
{

// Global device registry - EID -> DeviceNode pointer
std::map<uint8_t, std::unique_ptr<DeviceNode>> devicesByEID;

DeviceManager::DeviceManager(sdbusplus::bus_t& bus) : bus(bus)
{
    lg2::info("Initializing Device Manager");

    try
    {
        // Step 1: Initialize EntityManager interface
        emInterface = std::make_unique<EntityManagerInterface>(bus);

        // Step 2: Query existing devices and populate global registry
        initializeDeviceRegistry();

        // Step 3: Validate all registered devices
        validateAllRegisteredDevices();

        // Step 4: Setup monitoring for new devices from EntityManager
        emInterface->setupSignalMonitoring(
            [this](const PropertyMap& deviceProperties) {
                onEntityManagerDeviceAdded(deviceProperties);
            });

        // Step 5: Initialize MCTP endpoint monitoring
        mctpInterface = std::make_unique<MCTPInterface>(bus);

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

void DeviceManager::validateAllRegisteredDevices()
{
    lg2::info("Validating {COUNT} registered devices", "COUNT",
              devicesByEID.size());

    for (const auto& [eid, device] : devicesByEID)
    {
        lg2::debug("Validating device: {NAME} (EID {EID})", "NAME",
                   device->name, "EID", eid);
        physicalInterfaceCheck(*device);
    }

    lg2::info("Device validation completed");
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

            // Check physical interface
            physicalInterfaceCheck(*device);

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
