#include "physical_interface_check.hpp"

#include "device_error_logger.hpp"

#include <libusb-1.0/libusb.h>

#include <phosphor-logging/lg2.hpp>

#include <cctype>
#include <sstream>
#include <stdexcept>

namespace phosphor::device::manager
{

// Base class parsing implementation
bool PhysicalInterfaceChecker::parseInterface(const std::string& spec)
{
    tokens.clear();
    std::stringstream ss(spec);
    std::string token;

    while (std::getline(ss, token, ':'))
    {
        tokens.push_back(token);
    }

    return !tokens.empty();
}

bool PhysicalInterfaceChecker::parsePortPath(const std::string& portPathStr,
                                             std::vector<uint8_t>& portPath)
{
    portPath.clear();
    std::stringstream ss(portPathStr);
    std::string port;

    try
    {
        while (std::getline(ss, port, '.'))
        {
            portPath.push_back(static_cast<uint8_t>(std::stoi(port)));
        }
        return !portPath.empty();
    }
    catch (const std::exception& e)
    {
        lg2::error("Failed to parse port path {PATH}: {ERROR}", "PATH",
                   portPathStr, "ERROR", e.what());
        return false;
    }
}

USBChecker::USBChecker() : context(nullptr)
{
    libusb_context* ctx = nullptr;
    int ret = libusb_init(&ctx);
    if (ret != 0)
    {
        lg2::error("Failed to initialize libusb: {ERROR}", "ERROR",
                   libusb_error_name(ret));
        throw std::runtime_error("libusb initialization failed");
    }
    context = ctx;
    lg2::info("USB checker initialized successfully");
}

USBChecker::~USBChecker()
{
    if (context)
    {
        libusb_exit(static_cast<libusb_context*>(context));
    }
}

bool USBChecker::verify(const std::string& physicalInterface)
{
    if (!parseInterface(physicalInterface) || tokens.size() != 4)
    {
        lg2::error(
            "USB interface must have exactly 4 parts, got {COUNT} for: {SPEC}",
            "COUNT", tokens.size(), "SPEC", physicalInterface);
        return false;
    }

    try
    {
        // Parse USB interface from tokens:
        // "usb1:0x0955:0xcf11:1.1.1.1"
        USBInterface usbSpec;
        usbSpec.protocolBus = tokens[0]; // "usb1"

        // Extract bus number from "usb0" -> 0, "usb1" -> 1, etc.
        std::string busStr;
        for (char c : tokens[0])
        {
            if (std::isdigit(c))
            {
                busStr += c;
            }
        }
        usbSpec.busNumber =
            busStr.empty() ? 0 : static_cast<uint8_t>(std::stoi(busStr));

        usbSpec.vid = static_cast<uint16_t>(
            std::stoi(tokens[1].substr(2), nullptr, 16)); // "0x0955" -> 0x0955
        usbSpec.pid = static_cast<uint16_t>(
            std::stoi(tokens[2].substr(2), nullptr, 16)); // "0xcf11" -> 0xcf11

        if (!parsePortPath(tokens[3],
                           usbSpec.portPath)) // "1.1.1.1" -> [1,1,1,1]
        {
            lg2::error("Failed to parse USB port path: {PATH}", "PATH",
                       tokens[3]);
            return false;
        }

        lg2::info(
            "Parsed USB interface: Bus={BUS}, VID=0x{VID:04x}, PID=0x{PID:04x}, Port={PORT}",
            "BUS", usbSpec.busNumber, "VID", usbSpec.vid, "PID", usbSpec.pid,
            "PORT", tokens[3]);

        return verifyUSBDevice(usbSpec);
    }
    catch (const std::exception& e)
    {
        lg2::error("Exception parsing USB interface {SPEC}: {ERROR}", "SPEC",
                   physicalInterface, "ERROR", e.what());
        return false;
    }
}

bool USBChecker::verifyUSBDevice(const USBInterface& usbSpec)
{
    if (!context)
    {
        lg2::error("USB context not initialized");
        return false;
    }

    libusb_device** devices = nullptr;
    ssize_t deviceCount =
        libusb_get_device_list(static_cast<libusb_context*>(context), &devices);

    if (deviceCount < 0)
    {
        lg2::error("Failed to get USB device list: {ERROR}", "ERROR",
                   libusb_error_name(static_cast<int>(deviceCount)));
        return false;
    }

    bool found = false;

    for (ssize_t i = 0; i < deviceCount; ++i)
    {
        libusb_device_descriptor desc;
        int ret = libusb_get_device_descriptor(devices[i], &desc);
        if (ret != 0)
        {
            continue;
        }

        // Check VID/PID match
        if (desc.idVendor == usbSpec.vid && desc.idProduct == usbSpec.pid)
        {
            // Get actual bus number
            uint8_t actualBus = libusb_get_bus_number(devices[i]);

            // Check bus number match
            if (actualBus != usbSpec.busNumber)
            {
                lg2::info(
                    "USB device VID/PID match but bus mismatch: expected={EXPECTED}, actual={ACTUAL}",
                    "EXPECTED", usbSpec.busNumber, "ACTUAL", actualBus);
                continue;
            }

            // Check port path match
            auto actualPath = getDevicePortPath(devices[i]);
            if (matchesPortPath(actualPath, usbSpec.portPath))
            {
                lg2::info(
                    "USB device found: Bus={BUS}, VID=0x{VID:04x}, PID=0x{PID:04x}",
                    "BUS", actualBus, "VID", desc.idVendor, "PID",
                    desc.idProduct);
                found = true;
                break;
            }
            else
            {
                lg2::info(
                    "USB device VID/PID/bus match but port path mismatch");
            }
        }
    }

    libusb_free_device_list(devices, 1);

    if (!found)
    {
        lg2::info(
            "USB device not found: Bus={BUS}, VID=0x{VID:04x}, PID=0x{PID:04x}",
            "BUS", usbSpec.busNumber, "VID", usbSpec.vid, "PID", usbSpec.pid);
    }

    return found;
}

std::vector<uint8_t> USBChecker::getDevicePortPath(void* device)
{
    std::vector<uint8_t> path;

    // Get port numbers from device to root
    uint8_t portNumbers[8]; // USB 3.0 supports up to 7 tiers
    int pathLength = libusb_get_port_numbers(
        static_cast<libusb_device*>(device), portNumbers, sizeof(portNumbers));

    if (pathLength > 0)
    {
        path.assign(portNumbers, portNumbers + pathLength);
    }

    return path;
}

bool USBChecker::matchesPortPath(const std::vector<uint8_t>& actualPath,
                                 const std::vector<uint8_t>& expectedPath)
{
    if (actualPath.size() != expectedPath.size())
    {
        return false;
    }

    return std::equal(actualPath.begin(), actualPath.end(),
                      expectedPath.begin());
}

std::unique_ptr<PhysicalInterfaceChecker>
    PhysicalInterfaceCheckerFactory::createChecker(
        const std::string& physicalInterface)
{
    std::string protocol = parseProtocol(physicalInterface);

    if (protocol == "usb")
    {
        return std::make_unique<USBChecker>();
    }
    else if (protocol == "spi")
    {
        // Future: return std::make_unique<SPIChecker>();
        lg2::warning("SPI checker not yet implemented: {IFACE}", "IFACE",
                     physicalInterface);
        return nullptr;
    }
    else if (protocol == "i2c")
    {
        // Future: return std::make_unique<I2CChecker>();
        lg2::warning("I2C checker not yet implemented: {IFACE}", "IFACE",
                     physicalInterface);
        return nullptr;
    }

    lg2::warning("Unknown protocol '{PROTOCOL}' in interface: {IFACE}",
                 "PROTOCOL", protocol, "IFACE", physicalInterface);
    return nullptr;
}

std::string PhysicalInterfaceCheckerFactory::parseProtocol(
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

int PhysicalInterfaceCheckerFactory::parseBus(
    const std::string& physicalInterface)
{
    size_t colonPos = physicalInterface.find(':');
    if (colonPos == std::string::npos)
    {
        return -1;
    }

    std::string firstToken =
        physicalInterface.substr(0, colonPos); // "usb0" or "spi0"

    // Extract bus number (digits only)
    std::string busStr;
    for (char c : firstToken)
    {
        if (std::isdigit(c))
        {
            busStr += c;
        }
    }
    return busStr.empty() ? 0 : std::stoi(busStr);
}

void physicalInterfaceCheck(const DeviceNode& device)
{
    if (!device.hasPhysicalInterface())
    {
        lg2::debug("Device {NAME} has no physical interface to check", "NAME",
                   device.name);
        return; // No interface to verify is OK
    }

    // Only check hot-plug interfaces (USB). Skip non-hot-plug interfaces like
    // I2C, SPI which don't need runtime physical presence verification.
    std::string protocol = PhysicalInterfaceCheckerFactory::parseProtocol(
        device.physicalInterface);
    if (protocol != "usb")
    {
        lg2::debug(
            "Skipping physical interface check for non-hot-plug protocol {PROTOCOL}: {NAME}",
            "PROTOCOL", protocol, "NAME", device.name);
        return;
    }

    try
    {
        auto checker = PhysicalInterfaceCheckerFactory::createChecker(
            device.physicalInterface);
        if (!checker)
        {
            lg2::warning("No checker available for physical interface: {IFACE}",
                         "IFACE", device.physicalInterface);
            return;
        }

        bool result = checker->verify(device.physicalInterface);

        if (result)
        {
            lg2::info(
                "Physical interface verification PASSED for {NAME}: {IFACE}",
                "NAME", device.name, "IFACE", device.physicalInterface);
        }
        else
        {
            lg2::warning(
                "Physical interface verification FAILED for {NAME}: {IFACE}",
                "NAME", device.name, "IFACE", device.physicalInterface);

            // ALWAYS commit error if verification fails
            if (device.eid)
            {
                DeviceErrorLogger::commitPhysicalInterfaceError(*device.eid,
                                                                device.name);
            }
        }
    }
    catch (const std::exception& e)
    {
        lg2::error(
            "Exception during physical interface verification for {NAME}: {ERROR}",
            "NAME", device.name, "ERROR", e.what());

        // ALWAYS commit error on exception
        if (device.eid)
        {
            DeviceErrorLogger::commitPhysicalInterfaceError(*device.eid,
                                                            device.name);
        }
    }
}

} // namespace phosphor::device::manager
