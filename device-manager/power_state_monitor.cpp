#include "power_state_monitor.hpp"

#include "device_error_logger.hpp"

#include <phosphor-logging/lg2.hpp>
#include <sdbusplus/bus/match.hpp>

#include <map>
#include <string>
#include <variant>

namespace phosphor::device::manager
{

PowerStateMonitor::PowerStateMonitor(
    sdbusplus::bus_t& bus,
    const std::map<uint8_t, std::unique_ptr<DeviceNode>>& devices) :
    bus(bus), devices(devices)
{
    lg2::info("Initializing power state monitor");

    try
    {
        powerStateMatch = std::make_unique<sdbusplus::bus::match_t>(
            bus,
            sdbusplus::bus::match::rules::propertiesChanged(
                CHASSIS_STATE_PATH, CHASSIS_STATE_INTERFACE),
            [this](sdbusplus::message_t& msg) { handlePowerStateChange(msg); });

        lg2::info("Power state monitor initialized successfully");
    }
    catch (const std::exception& e)
    {
        lg2::error("Failed to initialize power state monitor: {ERROR}", "ERROR",
                   e.what());
        throw;
    }
}

void PowerStateMonitor::handlePowerStateChange(sdbusplus::message_t& msg)
{
    try
    {
        if (msg.is_method_error())
        {
            lg2::warning(
                "Power state PropertiesChanged signal contains method error");
            return;
        }

        std::string interface;
        using PropertyValue =
            std::variant<bool, uint8_t, int16_t, uint16_t, int32_t, uint32_t,
                         int64_t, uint64_t, double, std::string,
                         std::vector<uint8_t>, std::vector<std::string>>;
        std::map<std::string, PropertyValue> changedProperties;
        std::vector<std::string> invalidatedProperties;

        msg.read(interface, changedProperties, invalidatedProperties);

        auto it = changedProperties.find(POWER_STATE_PROPERTY);
        if (it == changedProperties.end())
        {
            return;
        }

        const auto* powerStatePtr = std::get_if<std::string>(&it->second);
        if (!powerStatePtr)
        {
            lg2::warning("CurrentPowerState property has unexpected type");
            return;
        }
        const auto& powerState = *powerStatePtr;
        lg2::info("Chassis power state changed to: {STATE}", "STATE",
                  powerState);

        // Find the BMC device - it is the root of all managed devices and
        // the source of the chassis power state signal
        auto parentIt =
            std::find_if(devices.begin(), devices.end(), [](const auto& pair) {
                return pair.second->deviceType == "BMC" &&
                       pair.second->eid.has_value();
            });

        if (parentIt == devices.end())
        {
            lg2::error(
                "No parent device found in registry - cannot commit power state error");
            return;
        }

        uint8_t parentEID = *parentIt->second->eid;
        const std::string& parentName = parentIt->second->name;

        if (powerState == POWER_STATE_OFF)
        {
            lg2::info(
                "Chassis powered off - committing standby error to parent {NAME} (EID {EID})",
                "NAME", parentName, "EID", parentEID);

            DeviceErrorLogger::commitPowerStandbyError(parentEID, parentName);
        }
        else if (powerState == POWER_STATE_ON)
        {
            lg2::info(
                "Chassis powered on - committing power on event to parent {NAME} (EID {EID})",
                "NAME", parentName, "EID", parentEID);

            DeviceErrorLogger::commitPowerOnEvent(parentEID, parentName);
        }
    }
    catch (const sdbusplus::exception_t& e)
    {
        lg2::error("D-Bus error handling power state change signal: {ERROR}",
                   "ERROR", e.what());
    }
    catch (const std::exception& e)
    {
        lg2::error("Error handling power state change signal: {ERROR}", "ERROR",
                   e.what());
    }
}

} // namespace phosphor::device::manager
