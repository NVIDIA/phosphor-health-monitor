#include "config.h"

#include "ipcHealthSensor.hpp"

#include "asio_connection.hpp"
#include "dbusIpcSensor.hpp"

#include <phosphor-logging/lg2.hpp>
#include <sdbusplus/bus.hpp>
#include <sdbusplus/message.hpp>
#include <sdbusplus/server/manager.hpp>

#include <cmath>
#include <numeric>
namespace phosphor
{
namespace ipc
{
PHOSPHOR_LOG2_USING;

std::shared_ptr<IPCHealthSensor> IPCHealthSensor::getIPCHealthSensor(
    sdbusplus::bus_t& bus, IPCConfig& ipcConfig, boost::asio::io_context& io)
{
    if (ipcConfig.name == "dbus")
    {
        return std::make_shared<phosphor::ipc::DBusIpcSensor>(bus, ipcConfig,
                                                              io);
    }
    return nullptr;
}

void IPCHealthSensor::initSensor()
{
    for (auto& paramConfig : ipcConfig.paramConfig)
    {
        // list the properties to be monitored
        statistics.push_back(
            std::make_pair(paramConfig.key, paramConfig.valueType));
    }

    try
    {
        // Initialize IPC sensor, override by derived class
        init();
    }
    catch (const std::exception& e)
    {
        lg2::error(
            "Exception occurred while initializing sensor {SERVICE}: {ERROR}",
            "SERVICE", ipcConfig.name, "ERROR", e);
    }
    timer.expires_after(std::chrono::seconds(ipcConfig.freq));
    timer.async_wait(std::bind(&IPCHealthSensor::readSensordata, this));
}
IPCHealthSensor::~IPCHealthSensor() {}

void IPCHealthSensor::readSensordata()
{
    try
    {
        readSensor();
    }
    catch (const std::exception& e)
    {
        lg2::error("Exception occurred while reading sensor {SERVICE}: {ERROR}",
                   "SERVICE", ipcConfig.name, "ERROR", e);
    }
    timer.expires_after(std::chrono::seconds(ipcConfig.freq));
    timer.async_wait(std::bind(&IPCHealthSensor::readSensordata, this));
}

void IPCHealthSensor::createThresholdLogEntry(const std::string& threshold,
                                              const std::string& serviceName,
                                              const std::string& proprtyName,
                                              double value,
                                              const double configThresholdValue)
{
    std::string messageId = "OpenBMC.0.4.";
    std::string messageArgs{};
    std::string messageLevel{};
    if (threshold == "warning")
    {
        messageId += "IPCWarningThresholdCrossed";
        messageArgs = serviceName + "," + proprtyName + "," +
                      std::to_string(value) + "," +
                      std::to_string(configThresholdValue);
        messageLevel = "xyz.openbmc_project.Logging.Entry.Level.Warning";
        createRFLogEntry(messageId, messageArgs, messageLevel);
    }
    else if (threshold == "critical")
    {
        messageId += "IPCCriticalThresholdCrossed";
        messageArgs = serviceName + "," + proprtyName + "," +
                      std::to_string(value) + "," +
                      std::to_string(configThresholdValue);
        messageLevel = "xyz.openbmc_project.Logging.Entry.Level.Critical";
        createRFLogEntry(messageId, messageArgs, messageLevel);
    }
    else
    {
        lg2::error("ERROR: Invalid threshold {TRESHOLD} used for log creation ",
                   "TRESHOLD", threshold);
    }
}

bool IPCHealthSensor::checkCriticalLogRateLimitWindow()
{
    if (!std::chrono::duration_cast<std::chrono::seconds>(
             lastCriticalLogLoggedTime.time_since_epoch())
             .count())
    {
        // Update the last Critical log loggedTime
        lastCriticalLogLoggedTime = std::chrono::high_resolution_clock::now();
        return true;
    }
    std::chrono::duration<double> diff =
        std::chrono::high_resolution_clock::now() - lastCriticalLogLoggedTime;
    if (diff.count() > ipcConfig.logRateLimit)
    {
        // Update the last Critical log loggedTime
        lastCriticalLogLoggedTime = std::chrono::high_resolution_clock::now();
        return true;
    }
    return false;
}

bool IPCHealthSensor::checkWarningLogRateLimitWindow()
{
    if (!std::chrono::duration_cast<std::chrono::seconds>(
             lastWarningLogLoggedTime.time_since_epoch())
             .count())
    {
        // Update the last warning log loggedTime
        lastWarningLogLoggedTime = std::chrono::high_resolution_clock::now();
        return true;
    }
    std::chrono::duration<double> diff =
        std::chrono::high_resolution_clock::now() - lastWarningLogLoggedTime;

    if (diff.count() > ipcConfig.logRateLimit)
    {
        // Update the last warning log loggedTime
        lastWarningLogLoggedTime = std::chrono::high_resolution_clock::now();
        return true;
    }
    return false;
}

// This function is used to check the sensor threshold and log the required
// message This is generic base class function overridden by derived class e.g.
// DBusIpcSensor
void IPCHealthSensor::checkSensorThreshold(const double value,
                                           const std::string& serviceName,
                                           struct ParamConfig cfg)
{
    if (std::isfinite(cfg.criticalHigh) &&
        (cfg.operatorType == "greater_than") && (value > cfg.criticalHigh))
    {
        lg2::error(
            "ASSERT: IPC service {SERVICE} is above the upper threshold critical "
            "high",
            "SERVICE", ipcConfig.name);
        if (checkCriticalLogRateLimitWindow())
        {
            lg2::info("Creating threshold log entry for critical");

            createThresholdLogEntry("critical", serviceName, cfg.key, value,
                                    cfg.criticalHigh);
            startUnit(cfg.criticalTgt, serviceName, "CrossedCriticalThreshold");
        }
    }
    else if (std::isfinite(cfg.criticalHigh) &&
             (cfg.operatorType == "less_than") && (value < cfg.criticalHigh))
    {
        // TODO: Add code to handle less than operator
    }
    else if (cfg.operatorType == "equal")
    {
        // TODO: Add code to handle equal than operator
    }

    if (std::isfinite(cfg.warningHigh) &&
        (cfg.operatorType == "greater_than") && (value > cfg.warningHigh))
    {
        lg2::error(
            "ASSERT: IPC service {SERVICE} is above the upper threshold warning high",
            "SERVICE", ipcConfig.name);
        if (checkWarningLogRateLimitWindow())
        {
            lg2::info("Creating threshold log entry for warning");
            createThresholdLogEntry("warning", serviceName, cfg.key, value,
                                    cfg.warningHigh);
        }
    }
    else if (std::isfinite(cfg.warningHigh) &&
             (cfg.operatorType == "less_than") && (value < cfg.warningHigh))
    {
        // TODO: Add code to handle less than operator
    }
    else if (cfg.operatorType == "equal")
    {
        // TODO: Add code to handle equal than operator
    }
}

// Create log entry implementation
void IPCHealthSensor::createRFLogEntry(const std::string& messageId,
                                       const std::string& messageArgs,
                                       const std::string& level)
{
    auto& connObject = AsioConnection::getAsioConnection();
    if (connObject == nullptr)
    {
        lg2::error("Connection object is null");
        return;
    }
    std::map<std::string, std::string> addData;
    addData["REDFISH_MESSAGE_ID"] = messageId;
    addData["REDFISH_MESSAGE_ARGS"] = messageArgs;
    // Make call to DBus Debug Service to create log entry
    connObject->async_method_call(
        [](const boost::system::error_code ec) {
        if (ec)
        {
            lg2::error("Phosphor logging Create resp_handler got error ");
            return;
        }
    }, "xyz.openbmc_project.Logging", "/xyz/openbmc_project/logging",
        "xyz.openbmc_project.Logging.Create", "Create", messageId, level,
        addData);
}

// Start unit implementation
void IPCHealthSensor::startUnit(const std::string& sysdUnit,
                                const std::string& serviceName,
                                const std::string& additionalData)
{
    auto& connObject = AsioConnection::getAsioConnection();
    if (connObject == nullptr)
    {
        lg2::error("Connection object is null");
        return;
    }
    if (sysdUnit.empty())
    {
        return;
    }
    auto service = sysdUnit;
    std::string args;
    args += "\\x20";
    args += serviceName;
    args += "\\x20";
    args += additionalData;

    std::replace(args.begin(), args.end(), '/', '-');
    auto p = service.find('@');
    if (p != std::string::npos)
        service.insert(p + 1, args);
    connObject->async_method_call(
        [](const boost::system::error_code ec) {
        if (ec)
        {
            lg2::error("StartUnit resp_handler got error ");
            return;
        }
    }, "org.freedesktop.systemd1", "/org/freedesktop/systemd1",
        "org.freedesktop.systemd1.Manager", "StartUnit", service, "replace");
}
} // namespace ipc
} // namespace phosphor
