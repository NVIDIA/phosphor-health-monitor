#pragma once

#include "device_node.hpp"

#include <sdbusplus/bus.hpp>
#include <sdbusplus/bus/match.hpp>

#include <map>
#include <memory>

namespace phosphor::device::manager
{

static constexpr auto CHASSIS_STATE_SERVICE =
    "xyz.openbmc_project.State.Chassis";
static constexpr auto CHASSIS_STATE_PATH =
    "/xyz/openbmc_project/state/chassis0";
static constexpr auto CHASSIS_STATE_INTERFACE =
    "xyz.openbmc_project.State.Chassis";
static constexpr auto POWER_STATE_PROPERTY = "CurrentPowerState";
static constexpr auto POWER_STATE_OFF =
    "xyz.openbmc_project.State.Chassis.PowerState.Off";
static constexpr auto POWER_STATE_ON =
    "xyz.openbmc_project.State.Chassis.PowerState.On";

/**
 * @brief Power State Monitor - watches chassis power state transitions
 *
 * Subscribes to PropertiesChanged on xyz.openbmc_project.State.Chassis.
 * On PowerState.Off: commits power standby error for each registered device.
 * On PowerState.On:  commits power on event (auto-clears standby errors).
 */
class PowerStateMonitor
{
  public:
    /**
     * @brief Constructor
     * @param bus D-Bus connection
     * @param devices Reference to the global device registry (EID ->
     * DeviceNode)
     */
    explicit PowerStateMonitor(
        sdbusplus::bus_t& bus,
        const std::map<uint8_t, std::unique_ptr<DeviceNode>>& devices);

    ~PowerStateMonitor() = default;

    PowerStateMonitor(const PowerStateMonitor&) = delete;
    PowerStateMonitor& operator=(const PowerStateMonitor&) = delete;
    PowerStateMonitor(PowerStateMonitor&&) = delete;
    PowerStateMonitor& operator=(PowerStateMonitor&&) = delete;

  private:
    /**
     * @brief Handler for PropertiesChanged signal on chassis state interface
     * @param msg D-Bus message containing the signal data
     */
    void handlePowerStateChange(sdbusplus::message_t& msg);

    /** @brief D-Bus connection */
    sdbusplus::bus_t& bus;

    /** @brief Reference to global device registry */
    const std::map<uint8_t, std::unique_ptr<DeviceNode>>& devices;

    /** @brief Match for chassis PropertiesChanged signals */
    std::unique_ptr<sdbusplus::bus::match_t> powerStateMatch;
};

} // namespace phosphor::device::manager
