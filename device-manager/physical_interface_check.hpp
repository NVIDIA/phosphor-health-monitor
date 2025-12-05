#pragma once

#include "device_node.hpp"

#include <cstdint>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

namespace phosphor::device::manager
{

/**
 * @brief USB interface specification
 * Format: "usb1:0x0955:0xcf11:1.1.1.1"
 * Fields: protocol+bus:VID:PID:portPath
 */
struct USBInterface
{
    std::string protocolBus; // "usb1"
    uint8_t busNumber;       // Extracted bus number (1 from "usb1")
    uint16_t vid;
    uint16_t pid;
    std::vector<uint8_t> portPath;
};

/**
 * @brief Base class for physical interface checking
 */
class PhysicalInterfaceChecker
{
  public:
    virtual ~PhysicalInterfaceChecker() = default;

    /**
     * @brief Verify physical interface presence
     * @param physicalInterface Interface specification string
     * @return true if interface is present and accessible
     */
    virtual bool verify(const std::string& physicalInterface) = 0;

  protected:
    std::vector<std::string> tokens; // Parsed colon-separated parts

    /**
     * @brief Parse interface specification into tokens
     * @param spec Interface specification (e.g., "usb0:0x0955:0xcf11:...")
     * @return true if parsing successful
     */
    bool parseInterface(const std::string& spec);

    /**
     * @brief Parse port path string into vector
     * @param portPathStr Port path string (e.g., "1.3.4")
     * @param portPath Output vector for port numbers
     * @return true if parsing successful
     */
    bool parsePortPath(const std::string& portPathStr,
                       std::vector<uint8_t>& portPath);
};

/**
 * @brief USB interface checker using libusb
 */
class USBChecker : public PhysicalInterfaceChecker
{
  public:
    USBChecker();
    ~USBChecker();

    /**
     * @brief Verify USB device presence
     * @param physicalInterface USB interface specification
     * @return true if USB device found and accessible
     */
    bool verify(const std::string& physicalInterface) override;

  private:
    void* context; // libusb_context*

    /**
     * @brief Verify specific USB device
     * @param usbSpec Parsed USB interface specification
     * @return true if device found with matching VID/PID and port path
     */
    bool verifyUSBDevice(const USBInterface& usbSpec);

    /**
     * @brief Get device port path from libusb device
     * @param device libusb_device pointer
     * @return Port path as vector of port numbers
     */
    std::vector<uint8_t> getDevicePortPath(void* device);

    /**
     * @brief Check if actual port path matches expected
     * @param actualPath Port path from USB device
     * @param expectedPath Port path from specification
     * @return true if paths match
     */
    bool matchesPortPath(const std::vector<uint8_t>& actualPath,
                         const std::vector<uint8_t>& expectedPath);
};

/**
 * @brief Factory for creating physical interface checkers
 */
class PhysicalInterfaceCheckerFactory
{
  public:
    /**
     * @brief Create checker based on physical interface type
     * @param physicalInterface Interface specification string
     * @return Appropriate checker instance or nullptr
     */
    static std::unique_ptr<PhysicalInterfaceChecker> createChecker(
        const std::string& physicalInterface);

    /**
     * @brief Parse protocol from interface specification
     * @param physicalInterface Interface specification (e.g., "usb0:...")
     * @return Protocol string (e.g., "usb") or empty if invalid
     */
    static std::string parseProtocol(const std::string& physicalInterface);

  private:
    /**
     * @brief Parse bus number from interface specification
     * @param physicalInterface Interface specification (e.g., "usb0:...")
     * @return Bus number (e.g., 0) or -1 if invalid
     */
    static int parseBus(const std::string& physicalInterface);
};

/**
 * @brief Check device physical interface and commit error if failed
 * @param device DeviceNode to check
 */
void physicalInterfaceCheck(const DeviceNode& device);

} // namespace phosphor::device::manager
