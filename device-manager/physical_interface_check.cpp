#include "physical_interface_check.hpp"

#include "device_error_logger.hpp"

#include <libusb-1.0/libusb.h>
#include <sys/time.h>

#include <phosphor-logging/lg2.hpp>

#include <algorithm>
#include <cctype>
#include <sstream>
#include <stdexcept>

namespace phosphor::device::manager
{

// ============================================================================
// USBHotplugMonitor Implementation
// ============================================================================

USBHotplugMonitor::USBHotplugMonitor(sdbusplus::async::context& ctx) :
    ctx(ctx), usbContext(nullptr), hotplugSupported(false),
    eventLoopRunning(false), removalCallbackHandle(0),
    removalCallbackRegistered(false)
{
    lg2::info("Initializing USB hotplug monitor");

    int ret = libusb_init(&usbContext);
    if (ret != 0)
    {
        lg2::error("Failed to initialize libusb: {ERROR}", "ERROR",
                   libusb_error_name(ret));
        throw std::runtime_error("libusb initialization failed");
    }

    // Check if hotplug is supported
    hotplugSupported = libusb_has_capability(LIBUSB_CAP_HAS_HOTPLUG) != 0;
    if (!hotplugSupported)
    {
        lg2::warning("USB hotplug not supported on this platform");
    }
    else
    {
        lg2::info("USB hotplug capability available");
        // Register global removal callback
        registerRemovalCallback();
    }
}

USBHotplugMonitor::~USBHotplugMonitor()
{
    lg2::info("Shutting down USB hotplug monitor");

    // Stop the event processing loop
    eventLoopRunning = false;

    // Deregister the global removal callback
    if (removalCallbackRegistered && usbContext)
    {
        libusb_hotplug_deregister_callback(usbContext, removalCallbackHandle);
        removalCallbackRegistered = false;
    }

    configuredDevices.clear();

    if (usbContext)
    {
        libusb_set_pollfd_notifiers(usbContext, nullptr, nullptr, nullptr);
        libusbBell.reset();
        libusbBellFd = -1;
        libusb_exit(usbContext);
        usbContext = nullptr;
    }
}

bool USBHotplugMonitor::isHotplugSupported() const
{
    return hotplugSupported;
}

std::string USBHotplugMonitor::parseProtocol(
    const std::string& physicalInterface)
{
    size_t colonPos = physicalInterface.find(':');
    if (colonPos == std::string::npos)
    {
        return "";
    }

    std::string firstToken =
        physicalInterface.substr(0, colonPos); // "usb0" or "spi0"

    // Extract protocol part (remove digits)
    std::string protocol;
    for (char c : firstToken)
    {
        if (std::isalpha(c))
        {
            protocol += c;
        }
    }
    return protocol; // "usb" or "spi"
}

bool USBHotplugMonitor::parseUSBInterface(const DeviceNode& device,
                                          USBDeviceInfo& info)
{
    // Parse "usb1:0x0955:0xcf11:1.1.1.1"
    std::stringstream ss(device.physicalInterface);
    std::string token;
    std::vector<std::string> tokens;

    while (std::getline(ss, token, ':'))
    {
        tokens.push_back(token);
    }

    if (tokens.size() != 4)
    {
        lg2::error("USB interface must have 4 parts, got {COUNT}: {SPEC}",
                   "COUNT", tokens.size(), "SPEC", device.physicalInterface);
        return false;
    }

    try
    {
        // Extract bus number from "usb0" -> 0, "usb1" -> 1
        std::string busStr;
        for (char c : tokens[0])
        {
            if (std::isdigit(c))
            {
                busStr += c;
            }
        }
        info.busNumber =
            busStr.empty() ? 0 : static_cast<uint8_t>(std::stoi(busStr));

        // Parse VID and PID
        info.vid =
            static_cast<uint16_t>(std::stoi(tokens[1].substr(2), nullptr, 16));
        info.pid =
            static_cast<uint16_t>(std::stoi(tokens[2].substr(2), nullptr, 16));

        // Parse port path
        info.portPath.clear();
        std::stringstream portSs(tokens[3]);
        std::string port;
        while (std::getline(portSs, port, '.'))
        {
            info.portPath.push_back(static_cast<uint8_t>(std::stoi(port)));
        }

        if (info.portPath.empty())
        {
            lg2::error("Failed to parse port path: {PATH}", "PATH", tokens[3]);
            return false;
        }

        info.eid = device.eid.value_or(0);
        info.deviceName = device.name;

        return true;
    }
    catch (const std::exception& e)
    {
        lg2::error("Exception parsing USB interface {SPEC}: {ERROR}", "SPEC",
                   device.physicalInterface, "ERROR", e.what());
        return false;
    }
}

bool USBHotplugMonitor::isDevicePresent(const USBDeviceInfo& info)
{
    if (!usbContext)
    {
        return false;
    }

    libusb_device** devices = nullptr;
    ssize_t count = libusb_get_device_list(usbContext, &devices);
    if (count < 0)
    {
        lg2::error("Failed to get USB device list: {ERROR}", "ERROR",
                   libusb_error_name(static_cast<int>(count)));
        return false;
    }

    bool found = false;
    for (ssize_t i = 0; i < count; ++i)
    {
        libusb_device_descriptor desc;
        if (libusb_get_device_descriptor(devices[i], &desc) != 0)
        {
            continue;
        }

        // Check VID/PID match
        if (desc.idVendor == info.vid && desc.idProduct == info.pid)
        {
            // Check bus number
            uint8_t bus = libusb_get_bus_number(devices[i]);
            if (bus == info.busNumber)
            {
                // Check port path
                auto portPath = getDevicePortPath(devices[i]);
                if (matchesPortPath(portPath, info.portPath))
                {
                    found = true;
                    break;
                }
            }
        }
    }

    libusb_free_device_list(devices, 1);
    return found;
}

void USBHotplugMonitor::registerRemovalCallback()
{
    if (removalCallbackRegistered)
    {
        return;
    }

    // Register for ALL device removals (VID/PID = MATCH_ANY)
    // We'll filter in handleDeviceRemoval based on configuredDevices
    int ret = libusb_hotplug_register_callback(
        usbContext, LIBUSB_HOTPLUG_EVENT_DEVICE_LEFT,
        static_cast<libusb_hotplug_flag>(0),
        LIBUSB_HOTPLUG_MATCH_ANY, // any VID
        LIBUSB_HOTPLUG_MATCH_ANY, // any PID
        LIBUSB_HOTPLUG_MATCH_ANY, // any device class
        hotplugCallback, this, &removalCallbackHandle);

    if (ret != LIBUSB_SUCCESS)
    {
        lg2::warning("Failed to register device removal callback: {ERROR}",
                     "ERROR", libusb_error_name(ret));
        return;
    }

    removalCallbackRegistered = true;
    lg2::info("Registered global USB device removal callback");

    libusb_set_pollfd_notifiers(usbContext, pollfdAddedCallback,
                                pollfdRemovedCallback, this);
    attachFirstPollfd(true);
}

bool USBHotplugMonitor::registerDevice(const DeviceNode& device)
{
    if (!device.hasPhysicalInterface())
    {
        lg2::debug("Device {NAME} has no physical interface", "NAME",
                   device.name);
        return false;
    }

    std::string protocol = parseProtocol(device.physicalInterface);
    if (protocol != "usb")
    {
        lg2::debug("Skipping non-USB device {NAME} for hotplug monitoring",
                   "NAME", device.name);
        return false;
    }

    if (!hotplugSupported)
    {
        lg2::warning("Cannot register device {NAME} - hotplug not supported",
                     "NAME", device.name);
        return false;
    }

    if (!device.eid)
    {
        lg2::warning("Cannot register device {NAME} - no EID", "NAME",
                     device.name);
        return false;
    }

    USBDeviceInfo info;
    if (!parseUSBInterface(device, info))
    {
        lg2::error("Failed to parse USB interface for device {NAME}", "NAME",
                   device.name);
        return false;
    }

    // Add to list of devices we care about
    configuredDevices.push_back(info);

    lg2::info(
        "Added USB device to monitoring list: {NAME} (Bus={BUS}, VID={VID}, PID={PID}, EID={EID})",
        "NAME", info.deviceName, "BUS", info.busNumber, "VID", lg2::hex,
        info.vid, "PID", lg2::hex, info.pid, "EID", info.eid);

    // Check if device is present NOW - log error immediately if not
    if (!isDevicePresent(info))
    {
        lg2::error(
            "USB device NOT present at boot: {NAME} (Bus={BUS}, VID={VID}, PID={PID}, EID={EID})",
            "NAME", info.deviceName, "BUS", info.busNumber, "VID", lg2::hex,
            info.vid, "PID", lg2::hex, info.pid, "EID", info.eid);

        DeviceErrorLogger::commitPhysicalInterfaceError(info.eid,
                                                        info.deviceName);
    }

    return true;
}

int LIBUSB_CALL USBHotplugMonitor::hotplugCallback(
    libusb_context* /*ctx*/, libusb_device* dev, libusb_hotplug_event event,
    void* userData)
{
    auto* self = static_cast<USBHotplugMonitor*>(userData);

    if (event == LIBUSB_HOTPLUG_EVENT_DEVICE_LEFT)
    {
        self->handleDeviceRemoval(dev);
    }

    // Return 0 to keep the callback registered
    return 0;
}

std::vector<uint8_t> USBHotplugMonitor::getDevicePortPath(libusb_device* device)
{
    std::vector<uint8_t> path;
    uint8_t portNumbers[8];
    int pathLength =
        libusb_get_port_numbers(device, portNumbers, sizeof(portNumbers));

    if (pathLength > 0)
    {
        path.assign(portNumbers, portNumbers + pathLength);
    }

    return path;
}

bool USBHotplugMonitor::matchesPortPath(
    const std::vector<uint8_t>& actualPath,
    const std::vector<uint8_t>& expectedPath)
{
    if (actualPath.size() != expectedPath.size())
    {
        return false;
    }
    return std::equal(actualPath.begin(), actualPath.end(),
                      expectedPath.begin());
}

void USBHotplugMonitor::handleDeviceRemoval(libusb_device* device)
{
    libusb_device_descriptor desc;
    int ret = libusb_get_device_descriptor(device, &desc);
    if (ret != 0)
    {
        lg2::error("Failed to get device descriptor for removed device");
        return;
    }

    uint8_t busNumber = libusb_get_bus_number(device);
    auto actualPortPath = getDevicePortPath(device);

    // Check if this matches any of our configured devices
    auto it = std::find_if(
        configuredDevices.begin(), configuredDevices.end(),
        [&](const USBDeviceInfo& info) {
            return info.vid == desc.idVendor && info.pid == desc.idProduct &&
                   info.busNumber == busNumber &&
                   matchesPortPath(actualPortPath, info.portPath);
        });

    if (it != configuredDevices.end())
    {
        lg2::error(
            "Configured USB device removed: {NAME} (Bus={BUS}, VID={VID}, PID={PID}, EID={EID})",
            "NAME", it->deviceName, "BUS", busNumber, "VID", lg2::hex,
            desc.idVendor, "PID", lg2::hex, desc.idProduct, "EID", it->eid);

        DeviceErrorLogger::commitPhysicalInterfaceError(it->eid,
                                                        it->deviceName);
    }
    // else: Some other USB device we don't care about - ignore
}

void USBHotplugMonitor::startEventProcessing()
{
    if (eventLoopRunning)
    {
        lg2::debug("Event processing already running");
        return;
    }

    if (!hotplugSupported)
    {
        lg2::warning("Cannot start event processing - hotplug not supported");
        return;
    }

    if (configuredDevices.empty())
    {
        lg2::debug("No devices configured, not starting event processing");
        return;
    }

    eventLoopRunning = true;
    lg2::info("Starting USB hotplug event processing loop");

    // Spawn the event processing coroutine
    ctx.spawn(eventProcessingLoop());
}

sdbusplus::async::task<> USBHotplugMonitor::eventProcessingLoop()
{
    using namespace std::chrono_literals;

    lg2::info("USB hotplug event loop started");

    while (eventLoopRunning && !ctx.stop_requested() && usbContext)
    {
        if (!libusbBell)
        {
            // No fd watcher yet - fall back to periodic polling
            processLibusbEvents();
            co_await sdbusplus::async::sleep_for(ctx, 1s);
            continue;
        }

        co_await libusbBell->next();
        processLibusbEvents();
    }

    eventLoopRunning = false;
    lg2::info("USB hotplug event loop stopped");
    co_return;
}

void USBHotplugMonitor::pollfdAddedCallback(int fd, short /*events*/,
                                            void* userData)
{
    auto* self = static_cast<USBHotplugMonitor*>(userData);
    if (self)
    {
        self->addWatcher(fd);
    }
}

void USBHotplugMonitor::pollfdRemovedCallback(int fd, void* userData)
{
    auto* self = static_cast<USBHotplugMonitor*>(userData);
    if (self)
    {
        self->removeWatcher(fd);
    }
}

void USBHotplugMonitor::addWatcher(int fd)
{
    if (libusbBell)
    {
        return;
    }
    try
    {
        libusbBell = std::make_unique<sdbusplus::async::fdio>(ctx, fd);
        libusbBellFd = fd;
    }
    catch (const std::exception& e)
    {
        lg2::error("Failed to create fdio watcher for FD {FD}: {ERROR}", "FD",
                   fd, "ERROR", e.what());
    }
}

void USBHotplugMonitor::removeWatcher(int fd)
{
    if (fd != libusbBellFd)
    {
        return;
    }
    libusbBell.reset();
    libusbBellFd = -1;
    attachFirstPollfd();
}

void USBHotplugMonitor::attachFirstPollfd(bool warnIfNull)
{
    const libusb_pollfd** pfds = libusb_get_pollfds(usbContext);
    if (!pfds)
    {
        if (warnIfNull)
        {
            lg2::warning("libusb_get_pollfds returned null");
        }
        return;
    }
    if (*pfds != nullptr)
    {
        addWatcher((*pfds)->fd);
    }
    libusb_free_pollfds(pfds);
}

void USBHotplugMonitor::processLibusbEvents()
{
    struct timeval tv = {0, 0};
    libusb_handle_events_timeout_completed(usbContext, &tv, nullptr);
}

} // namespace phosphor::device::manager
