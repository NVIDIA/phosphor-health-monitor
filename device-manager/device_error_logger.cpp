#include "device_error_logger.hpp"

#include <phosphor-logging/lg2.hpp>
#include <sdbusplus/bus.hpp>

#include <map>
#include <string>
#include <tuple>
#include <vector>

namespace phosphor::device::manager
{

void DeviceErrorLogger::clearDeviceErrors(sdbusplus::bus_t& bus, uint8_t eid)
{
    std::string deviceName = "EID_" + std::to_string(eid);
    std::string objectPath =
        "/com/nvidia/state/device_status/" + std::to_string(eid);

    lg2::info("Clearing errors for device: {DEVICE_NAME} (EID: {EID})",
              "DEVICE_NAME", deviceName, "EID", eid);

    try
    {
        // Create the DeviceStatus property value structure
        // Signature: a{s(sa(xsa{ss}))} = array of dict entries where:
        // - key: string (status type)
        // - value: struct of (string: health state, array of error details)
        using ErrorDetail = std::tuple<int64_t, std::string,
                                       std::map<std::string, std::string>>;
        using StatusValue = std::tuple<std::string, std::vector<ErrorDetail>>;
        using DeviceStatusMap = std::map<std::string, StatusValue>;

        // Build the healthy status structure
        DeviceStatusMap deviceStatus;
        deviceStatus["com.nvidia.State.DeviceState.StatusType.Communication"] =
            std::make_tuple("com.nvidia.State.DeviceState.DeviceHealth.Healthy",
                            std::vector<ErrorDetail>{});

        // Convert to D-Bus variant
        std::variant<DeviceStatusMap> statusVariant = deviceStatus;

        // Make async D-Bus call to set DeviceStatus property
        auto method = bus.new_method_call(
            "xyz.openbmc_project.Logging", objectPath.c_str(),
            "org.freedesktop.DBus.Properties", "Set");

        method.append("com.nvidia.State.DeviceState", "DeviceStatus",
                      statusVariant);

        // Use synchronous call with no-reply flag for fire-and-forget behavior
        bus.call(method);

        lg2::info(
            "Successfully cleared errors for device: {DEVICE} (EID: {EID})",
            "DEVICE", deviceName, "EID", eid);
    }
    catch (const std::exception& e)
    {
        lg2::error(
            "Exception while clearing errors for device: {DEVICE_NAME}, error: {ERROR}",
            "DEVICE_NAME", deviceName, "ERROR", e.what());
    }
}

} // namespace phosphor::device::manager
