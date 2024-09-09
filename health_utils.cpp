#include "health_utils.hpp"

#include <dirent.h>

#include <phosphor-logging/lg2.hpp>
#include <xyz/openbmc_project/ObjectMapper/client.hpp>

#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
PHOSPHOR_LOG2_USING;

namespace phosphor::health::utils
{

void startUnit(sdbusplus::bus_t& bus, const std::string& sysdUnit,
               const std::string resource, const std::string path,
               const std::string binaryname, const double usage)
{
    if (sysdUnit.empty())
    {
        return;
    }
    info("Starting systemd unit {UNIT} with resource {RESOURCE} path {PATH} "
         "binaryname {BINARYNAME} usage {USAGE}",
         "UNIT", sysdUnit, "RESOURCE", resource, "PATH", path, "BINARYNAME",
         binaryname, "USAGE", usage);
    auto service = sysdUnit;
    std::string args;
    args += "\\x20";
    args += resource;
    args += "\\x20";
    if (!path.empty())
    {
        args += path;
        args += "\\x20";
    }
    if (!binaryname.empty())
    {
        args += binaryname;
        args += "\\x20";
    }
    if (usage > 0)
    {
        args += std::to_string(usage);
        args += "\\x20";
    }

    std::replace(args.begin(), args.end(), '/', '-');
    auto p = service.find('@');
    if (p != std::string::npos)
        service.insert(p + 1, args);
    info("Starting systemd unit {UNIT}", "UNIT", service);
    sdbusplus::message_t msg = bus.new_method_call(
        "org.freedesktop.systemd1", "/org/freedesktop/systemd1",
        "org.freedesktop.systemd1.Manager", "StartUnit");
    msg.append(service, "replace");
    bus.call_noreply(msg);
}

auto findPaths(sdbusplus::async::context& ctx, const std::string& iface,
               const std::string& subpath) -> sdbusplus::async::task<paths_t>
{
    try
    {
        using ObjectMapper =
            sdbusplus::client::xyz::openbmc_project::ObjectMapper<>;

        auto mapper = ObjectMapper(ctx)
                          .service(ObjectMapper::default_service)
                          .path(ObjectMapper::instance_path);

        std::vector<std::string> ifaces = {iface};
        co_return co_await mapper.get_sub_tree_paths(subpath, 0, ifaces);
    }
    catch (std::exception& e)
    {
        error("Exception occurred for GetSubTreePaths for {PATH}: {ERROR}",
              "PATH", subpath, "ERROR", e);
    }
    co_return {};
}

bool containsOnlyDigits(const std::string& str)
{
    for (char c : str)
    {
        if (!isdigit(c))
        {
            return false;
        }
    }
    return true;
}

int getSystemClockFrequency()
{
    // get the number of clock ticks per second
    int hertz = sysconf(_SC_CLK_TCK);
    if (hertz <= 0)
    {
        hertz = 100; // assuming normal linux system
    }
    return hertz;
}

int getNumberofCPU()
{
    // get the number of processors in system
    int cpus = sysconf(_SC_NPROCESSORS_ONLN);
    if (cpus <= 0)
    {
        cpus = 1;
    }
    return cpus;
}
void createThresholdLogEntry(sdbusplus::bus_t& bus, Threshold::Type& type,
                             Threshold::Bound& bound,
                             const std::string& sensorName, double value,
                             const double configThresholdValue)
{
    std::string messageId = "OpenBMC.0.4.";
    std::string messageArgs{};
    std::string messageLevel{};
    std::string resolution{};

    if (type == Threshold::Type::Warning && bound == Threshold::Bound::Upper)
    {
        messageId += "SensorThresholdWarningHighGoingHigh";
        messageArgs = sensorName + "," + std::to_string(value) + "," +
                      std::to_string(configThresholdValue);
        messageLevel = "xyz.openbmc_project.Logging.Entry.Level.Warning";
        resolution = "None";
        createRFLogEntry(bus, messageId, messageArgs, messageLevel, resolution);
    }
    else if (type == Threshold::Type::Critical &&
             bound == Threshold::Bound::Upper)
    {
        messageId += "SensorThresholdCriticalHighGoingHigh";
        messageArgs = sensorName + "," + std::to_string(value) + "," +
                      std::to_string(configThresholdValue);
        messageLevel = "xyz.openbmc_project.Logging.Entry.Level.Critical";
        resolution = "None";
        createRFLogEntry(bus, messageId, messageArgs, messageLevel, resolution);
    }
    else if (type == Threshold::Type::Warning &&
             bound == Threshold::Bound::Lower)
    {
        messageId += "SensorThresholdWarningLowGoingLow";
        messageArgs = sensorName + "," + std::to_string(value) + "," +
                      std::to_string(configThresholdValue);
        messageLevel = "xyz.openbmc_project.Logging.Entry.Level.Warning";
        resolution = "None";
        createRFLogEntry(bus, messageId, messageArgs, messageLevel, resolution);
    }
    else if (type == Threshold::Type::Critical &&
             bound == Threshold::Bound::Lower)
    {
        messageId += "SensorThresholdCriticalLowGoingLow";
        messageArgs = sensorName + "," + std::to_string(value) + "," +
                      std::to_string(configThresholdValue);
        messageLevel = "xyz.openbmc_project.Logging.Entry.Level.Critical";
        resolution = "None";
        createRFLogEntry(bus, messageId, messageArgs, messageLevel, resolution);
    }
    else
    {
        error("ERROR: Invalid threshold {TRESHOLD} used for log creation ",
              "TRESHOLD", type);
    }
}
void createRFLogEntry(sdbusplus::bus_t& bus, const std::string& messageId,
                      const std::string& messageArgs, const std::string& level,
                      const std::string& resolution)
{
    auto method = bus.new_method_call(
        "xyz.openbmc_project.Logging", "/xyz/openbmc_project/logging",
        "xyz.openbmc_project.Logging.Create", "Create");
    // Signature is ssa{ss}
    method.append(messageId);
    method.append(level);
    method.append(std::array<std::pair<std::string, std::string>, 3>(
        {std::pair<std::string, std::string>({"REDFISH_MESSAGE_ID", messageId}),
         std::pair<std::string, std::string>(
             {"REDFISH_MESSAGE_ARGS", messageArgs}),
         std::pair<std::string, std::string>(
             {"xyz.openbmc_project.Logging.Entry.Resolution", resolution})}));
    try
    {
        // A strict timeout for logging service to fail early and ensure
        // the original caller does not encounter dbus timeout
        uint64_t timeout_us = 10000000;

        bus.call_noreply(method, timeout_us);
    }
    catch (const sdbusplus::exception::exception& e)
    {
        error("Failed to create log entry, exception:{ERROR}", "ERROR", e);
    }
}

} // namespace phosphor::health::utils
