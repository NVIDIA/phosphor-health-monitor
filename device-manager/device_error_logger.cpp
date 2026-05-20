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

void DeviceErrorLogger::commitPhysicalInterfaceError(
    uint8_t eid, const std::string& deviceName)
{
    try
    {
        using namespace nv::lg2;

        // Use physical interface absent error code
        int64_t errorCode = ErrorCode::PhysicalInterface::ABSENT;
        // Fallback to EID if device name is empty
        std::string name =
            deviceName.empty() ? ("EID_" + std::to_string(eid)) : deviceName;

        std::string errorMessage =
            "Device was not discovered on the USB physical interface";
        std::string resolution =
            "Retry firmware update operation, if problem persists, follow FW upgrade recovery flow.";

        std::map<std::string, std::string> additionalData = {
            {"REDFISH_MESSAGE_ID", "ResourceEvent.1.0.ResourceErrorsDetected"},
            {"REDFISH_MESSAGE_ARGS", name + ", " + errorMessage},
            {"REDFISH_RESOLUTION", resolution},
            {"REDFISH_SEVERITY",
             "xyz.openbmc_project.Logging.Entry.Level.Informational"},
            {"REDFISH_ORIGIN_OF_CONDITION", name}};

        CommitDeviceError(eid, errorCode, ErrorClass::PhysicalInterface,
                          additionalData);
    }
    catch (const std::exception& e)
    {
        lg2::error(
            "Exception while committing Physical Interface error for EID {EID}: {ERROR}",
            "EID", eid, "ERROR", e.what());
    }
}

void DeviceErrorLogger::commitPowerStandbyError(uint8_t eid,
                                                const std::string& deviceName)
{
    try
    {
        using namespace nv::lg2;

        std::string name =
            deviceName.empty() ? ("EID_" + std::to_string(eid)) : deviceName;

        std::string errorMessage =
            "Device not powered on as the system is in standby power";
        std::string resolution =
            "Ensure all devices are powered ON and system is in DC ON state.";

        std::map<std::string, std::string> additionalData = {
            {"REDFISH_MESSAGE_ID", "ResourceEvent.1.0.ResourceErrorsDetected"},
            {"REDFISH_MESSAGE_ARGS", name + ", " + errorMessage},
            {"REDFISH_RESOLUTION", resolution},
            {"REDFISH_SEVERITY",
             "xyz.openbmc_project.Logging.Entry.Level.Informational"},
            {"REDFISH_ORIGIN_OF_CONDITION", name}};

        lg2::info(
            "Committing power standby error for device {DEVICE} (EID: {EID})",
            "DEVICE", name, "EID", eid);

        CommitDeviceError(eid, ErrorCode::PowerStatus::POWER_OFF,
                          ErrorClass::Power, additionalData);
    }
    catch (const std::exception& e)
    {
        lg2::error(
            "Exception while committing power standby error for EID {EID}: {ERROR}",
            "EID", eid, "ERROR", e.what());
    }
}

void DeviceErrorLogger::commitPowerOnEvent(uint8_t eid,
                                           const std::string& deviceName)
{
    try
    {
        using namespace nv::lg2;

        std::string name =
            deviceName.empty() ? ("EID_" + std::to_string(eid)) : deviceName;

        std::string errorMessage = "Device powered on successfully";
        std::string resolution = "";

        std::map<std::string, std::string> additionalData = {
            {"REDFISH_MESSAGE_ID", "ResourceEvent.1.0.ResourceStatusChangedOK"},
            {"REDFISH_MESSAGE_ARGS", name + ", " + errorMessage},
            {"REDFISH_RESOLUTION", resolution},
            {"REDFISH_SEVERITY", "OK"},
            {"REDFISH_ORIGIN_OF_CONDITION", name}};

        lg2::info("Committing power on event for device {DEVICE} (EID: {EID})",
                  "DEVICE", name, "EID", eid);

        CommitDeviceError(eid, ErrorCode::PowerStatus::POWER_ON,
                          ErrorClass::Power, additionalData);
    }
    catch (const std::exception& e)
    {
        lg2::error(
            "Exception while committing power on event for EID {EID}: {ERROR}",
            "EID", eid, "ERROR", e.what());
    }
}

} // namespace phosphor::device::manager
