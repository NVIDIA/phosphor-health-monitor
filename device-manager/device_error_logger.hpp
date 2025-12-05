#pragma once

#include <phosphor-logging/device_error_log.hpp>
#include <sdbusplus/bus.hpp>

#include <cstdint>
#include <string>

namespace phosphor::device::manager
{

/**
 * @brief Device Error Logger - handles clearing device errors from D-Bus
 * logging service
 */
class DeviceErrorLogger
{
  public:
    /**
     * @brief Clear all errors associated with a device EID using D-Bus call
     * @param bus D-Bus connection
     * @param eid MCTP endpoint ID of the device
     */
    static void clearDeviceErrors(sdbusplus::bus_t& bus, uint8_t eid);

    /**
     * @brief Commit physical interface error
     * @param eid Device EID (address)
     * @param deviceName Device name
     */
    static void commitPhysicalInterfaceError(uint8_t eid,
                                             const std::string& deviceName);
};

} // namespace phosphor::device::manager
