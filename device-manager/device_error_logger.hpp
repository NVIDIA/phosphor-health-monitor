#pragma once

#include <sdbusplus/bus.hpp>

#include <cstdint>

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
     * @brief Clear all errors associated with a device EID using async D-Bus
     * call
     * @param bus D-Bus connection
     * @param eid MCTP endpoint ID of the device
     */
    static void clearDeviceErrors(sdbusplus::bus_t& bus, uint8_t eid);
};

} // namespace phosphor::device::manager
