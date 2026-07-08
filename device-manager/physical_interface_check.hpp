#pragma once

#include "device_node.hpp"

#include <libusb.h>

#include <sdbusplus/async.hpp>

#include <cstdint>
#include <string>
#include <vector>

namespace phosphor::device::manager
{

/**
 * @brief Information about a USB device for hotplug monitoring
 */
struct USBDeviceInfo
{
    uint8_t busNumber;
    uint16_t vid;
    uint16_t pid;
    std::vector<uint8_t> portPath;
    uint8_t eid;
    std::string deviceName;
};

/**
 * @brief USB hotplug monitor using libusb hotplug API
 *
 * Monitors USB device removal events and logs errors when configured
 * devices are physically disconnected. Uses a single global callback
 * for all removal events and filters against configured devices.
 */
class USBHotplugMonitor
{
  public:
    /**
     * @brief Construct USB hotplug monitor
     * @param ctx sdbusplus async context for event loop integration
     */
    explicit USBHotplugMonitor(sdbusplus::async::context& ctx);

    /**
     * @brief Destructor - cleanup libusb resources
     */
    ~USBHotplugMonitor();

    // Non-copyable, non-movable
    USBHotplugMonitor(const USBHotplugMonitor&) = delete;
    USBHotplugMonitor& operator=(const USBHotplugMonitor&) = delete;
    USBHotplugMonitor(USBHotplugMonitor&&) = delete;
    USBHotplugMonitor& operator=(USBHotplugMonitor&&) = delete;

    /**
     * @brief Register a USB device for hotplug monitoring
     *
     * Adds device to configured devices list and checks if present.
     * If device is NOT present at boot, logs error immediately.
     *
     * @param device DeviceNode containing USB physical interface info
     * @return true if device added successfully
     */
    bool registerDevice(const DeviceNode& device);

    /**
     * @brief Check if hotplug monitoring is supported
     * @return true if libusb hotplug is available
     */
    bool isHotplugSupported() const;

    /**
     * @brief Start processing libusb events
     * Must be called after devices are registered to begin monitoring
     */
    void startEventProcessing();

    /**
     * @brief Static hotplug callback for libusb
     */
    static int LIBUSB_CALL hotplugCallback(
        libusb_context* ctx, libusb_device* dev, libusb_hotplug_event event,
        void* userData);

  private:
    /**
     * @brief Coroutine to periodically process libusb events
     */
    sdbusplus::async::task<> eventProcessingLoop();

    /**
     * @brief Handle device removal - called from hotplug callback
     * @param device libusb device that was removed
     */
    void handleDeviceRemoval(libusb_device* device);

    /**
     * @brief Check if USB device is currently present on the bus
     * @param info USB device info to check
     * @return true if device is found with matching bus, VID, PID, port path
     */
    bool isDevicePresent(const USBDeviceInfo& info);

    /**
     * @brief Parse USB interface string into USBDeviceInfo
     * @param device DeviceNode to parse
     * @param info Output USBDeviceInfo
     * @return true if parsing successful
     */
    bool parseUSBInterface(const DeviceNode& device, USBDeviceInfo& info);

    /**
     * @brief Get port path from libusb device
     * @param device libusb device pointer
     * @return Port path as vector
     */
    std::vector<uint8_t> getDevicePortPath(libusb_device* device);

    /**
     * @brief Check if port paths match
     */
    bool matchesPortPath(const std::vector<uint8_t>& actualPath,
                         const std::vector<uint8_t>& expectedPath);

    /**
     * @brief Parse protocol from interface specification
     * @param physicalInterface Interface specification (e.g., "usb0:...")
     * @return Protocol string (e.g., "usb") or empty if invalid
     */
    static std::string parseProtocol(const std::string& physicalInterface);

    /**
     * @brief Register global callback for device removal events
     */
    void registerRemovalCallback();

    sdbusplus::async::context& ctx;
    libusb_context* usbContext;
    bool hotplugSupported;
    bool eventLoopRunning;

    // List of configured USB devices we care about
    std::vector<USBDeviceInfo> configuredDevices;

    // Handle for the global DEVICE_LEFT callback
    libusb_hotplug_callback_handle removalCallbackHandle;
    bool removalCallbackRegistered;
};

} // namespace phosphor::device::manager
