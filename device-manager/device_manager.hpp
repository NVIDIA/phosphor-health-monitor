#pragma once

#include "device_node.hpp"
#include "em_interface.hpp"
#include "mctp_interface.hpp"
#include "physical_interface_check.hpp"

#include <sdbusplus/async.hpp>
#include <sdbusplus/bus.hpp>

#include <map>
#include <memory>

namespace phosphor::device::manager
{

// Extern global device registry
extern std::map<uint8_t, std::unique_ptr<DeviceNode>> devicesByEID;

class DeviceManager
{
  public:
    explicit DeviceManager(sdbusplus::async::context& ctx);

  private:
    void initializeDeviceRegistry();
    void registerDevicesForHotplugMonitoring();
    void onEntityManagerDeviceAdded(const PropertyMap& properties);

    sdbusplus::async::context& ctx;
    std::unique_ptr<EntityManagerInterface> emInterface;

    // MCTP endpoint monitoring - owned by DeviceManager
    std::unique_ptr<MCTPInterface> mctpInterface;

    // USB hotplug monitoring
    std::unique_ptr<USBHotplugMonitor> usbHotplugMonitor;
};

} // namespace phosphor::device::manager
