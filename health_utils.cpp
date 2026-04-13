#include "health_utils.hpp"

#include "ipc/asio_connection.hpp"

#include <dirent.h>

#include <phosphor-logging/lg2.hpp>
#include <xyz/openbmc_project/ObjectMapper/client.hpp>

#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <unordered_set>
PHOSPHOR_LOG2_USING;

namespace phosphor::health::utils
{

static const std::unordered_set<std::string> systemdReplaceIrreversiblyTarget{
    "halt.target",        "poweroff.target", "reboot.target",
    "soft-reboot.target", "kexec.target",    "exit.target"};

void startUnit(sdbusplus::bus_t& bus, const std::string& sysdUnit,
               const std::string resource, const std::string path,
               const std::string binaryname, const double usage)
{
    if (sysdUnit.empty())
    {
        return;
    }
    info("Starting systemd unit {UNIT} with resource {RESOURCE}", "UNIT",
         sysdUnit, "RESOURCE", resource);
    auto service = sysdUnit;
    std::string args;
    args += "\\x20";
    args += resource;
    args += "\\x20";
    if (!path.empty())
    {
        info("paths={PATH}", "PATH", path);
        args += path;
        args += "\\x20";
    }
    if (!binaryname.empty())
    {
        info("binaryname={BINARYNAME}", "BINARYNAME", binaryname);
        args += binaryname;
        args += "\\x20";
    }
    if (usage > 0)
    {
        info("usage={USAGE}", "USAGE", usage);
        args += std::to_string(usage);
        args += "\\x20";
    }

    std::replace(args.begin(), args.end(), '/', '-');
    auto p = service.find('@');
    if (p != std::string::npos)
        service.insert(p + 1, args);
    sdbusplus::message_t msg = bus.new_method_call(
        "org.freedesktop.systemd1", "/org/freedesktop/systemd1",
        "org.freedesktop.systemd1.Manager", "StartUnit");
    if (systemdReplaceIrreversiblyTarget.contains(service))
    {
        msg.append(service, "replace-irreversibly");
    }
    else
    {
        msg.append(service, "replace");
    }
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
        messageLevel = "xyz.openbmc_project.Logging.Entry.Level.Informational";
        resolution = "None";
        createRFLogEntry(bus, messageId, messageArgs, messageLevel, resolution);
    }
    else if (type == Threshold::Type::Critical &&
             bound == Threshold::Bound::Upper)
    {
        messageId += "SensorThresholdCriticalHighGoingHigh";
        messageArgs = sensorName + "," + std::to_string(value) + "," +
                      std::to_string(configThresholdValue);
        messageLevel = "xyz.openbmc_project.Logging.Entry.Level.Informational";
        resolution = "None";
        createRFLogEntry(bus, messageId, messageArgs, messageLevel, resolution);
    }
    else if (type == Threshold::Type::Warning &&
             bound == Threshold::Bound::Lower)
    {
        messageId += "SensorThresholdWarningLowGoingLow";
        messageArgs = sensorName + "," + std::to_string(value) + "," +
                      std::to_string(configThresholdValue);
        messageLevel = "xyz.openbmc_project.Logging.Entry.Level.Informational";
        resolution = "None";
        createRFLogEntry(bus, messageId, messageArgs, messageLevel, resolution);
    }
    else if (type == Threshold::Type::Critical &&
             bound == Threshold::Bound::Lower)
    {
        messageId += "SensorThresholdCriticalLowGoingLow";
        messageArgs = sensorName + "," + std::to_string(value) + "," +
                      std::to_string(configThresholdValue);
        messageLevel = "xyz.openbmc_project.Logging.Entry.Level.Informational";
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
    method.append(std::array<std::pair<std::string, std::string>, 4>(
        {std::pair<std::string, std::string>({"REDFISH_MESSAGE_ID", messageId}),
         std::pair<std::string, std::string>(
             {"REDFISH_MESSAGE_ARGS", messageArgs}),
         std::pair<std::string, std::string>(
             {"xyz.openbmc_project.Logging.Entry.Resolution", resolution}),
         std::pair<std::string, std::string>({"namespace", "Manager"})}));
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

void asyncCreateRFLogEntry(
    const std::string& messageID, const std::string& messageArgs,
    const std::string& messageLevel, const std::string& resolution,
    const std::string& logNamespace)
{
    auto& connObject = phosphor::ipc::AsioConnection::getAsioConnection();
    if (connObject == nullptr)
    {
        error("Connection object is null");
        return;
    }

    std::map<std::string, std::string> addData;
    addData["REDFISH_MESSAGE_ID"] = messageID;

    if (!messageArgs.empty())
    {
        addData["REDFISH_MESSAGE_ARGS"] = messageArgs;
    }

    if (!resolution.empty())
    {
        addData["xyz.openbmc_project.Logging.Entry.Resolution"] = resolution;
    }

    if (!logNamespace.empty())
    {
        addData["namespace"] = logNamespace;
    }

    connObject->async_method_call(
        [](boost::system::error_code ec) {
            if (ec)
            {
                error("error while logging message registry: ", "ERROR_MESSAGE",
                      ec.message());
                return;
            }
        },
        "xyz.openbmc_project.Logging", "/xyz/openbmc_project/logging",
        "xyz.openbmc_project.Logging.Create", "Create", messageID, messageLevel,
        addData);
    return;
}

} // namespace phosphor::health::utils
