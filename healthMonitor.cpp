#include "config.h"

#include "healthMonitor.hpp"

#include <unistd.h>

#include <boost/asio/steady_timer.hpp>
#include <sdbusplus/asio/connection.hpp>
#include <sdbusplus/asio/object_server.hpp>
#include <sdbusplus/asio/sd_event.hpp>
#include <sdbusplus/bus/match.hpp>
#include <sdbusplus/server/manager.hpp>
#include <sdeventplus/event.hpp>

#include <fstream>
#include <iostream>
#include <memory>
#include <numeric>
#include <sstream>
#include <string>
#include <cstring>
#include <dirent.h>
#include <map>
#include <any>
extern "C"
{
#include <sys/statvfs.h>
#include <sys/sysinfo.h>
}

PHOSPHOR_LOG2_USING;

static constexpr bool DEBUG = false;
static constexpr uint8_t defaultHighThreshold = 100;

// Limit sensor recreation interval to 120s
bool needUpdate;

int cpus;
int hertz;
static constexpr int TIMER_INTERVAL = 120;
std::shared_ptr<boost::asio::steady_timer> sensorRecreateTimer;
std::shared_ptr<phosphor::health::HealthMon> healthMon;

namespace phosphor
{
namespace health
{

// Example values for iface:
// BMC_CONFIGURATION
// BMC_INVENTORY_ITEM
std::vector<std::string> findPathsWithType(sdbusplus::bus_t& bus,
                                           const std::string& iface)
{
    PHOSPHOR_LOG2_USING;
    std::vector<std::string> ret;

    // Find all BMCs (DBus objects implementing the
    // Inventory.Item.Bmc interface that may be created by
    // configuring the Inventory Manager)
    sdbusplus::message_t msg = bus.new_method_call(
        "xyz.openbmc_project.ObjectMapper",
        "/xyz/openbmc_project/object_mapper",
        "xyz.openbmc_project.ObjectMapper", "GetSubTreePaths");

    // "/": No limit for paths for all the paths that may be touched
    // in this daemon

    // 0: Limit the depth to 0 to match both objects created by
    // EntityManager and by InventoryManager

    // {iface}: The endpoint of the Association Definition must have
    // the Inventory.Item.Bmc interface
    msg.append("/", 0, std::vector<std::string>{iface});

    try
    {
        bus.call(msg, 0).read(ret);

        if (!ret.empty())
        {
            debug("{IFACE} found", "IFACE", iface);
        }
        else
        {
            debug("{IFACE} not found", "IFACE", iface);
        }
    }
    catch (std::exception& e)
    {
        error("Exception occurred while calling {PATH}: {ERROR}", "PATH",
              InventoryPath, "ERROR", e);
    }
    return ret;
}

enum CPUStatesTime
{
    USER_IDX = 0,
    NICE_IDX,
    SYSTEM_IDX,
    IDLE_IDX,
    IOWAIT_IDX,
    IRQ_IDX,
    SOFTIRQ_IDX,
    STEAL_IDX,
    GUEST_USER_IDX,
    GUEST_NICE_IDX,
    NUM_CPU_STATES_TIME
};

// # cat /proc/stat|grep 'cpu '
// cpu  5750423 14827 1572788 9259794 1317 0 28879 0 0 0
static_assert(NUM_CPU_STATES_TIME == 10);

enum CPUUtilizationType
{
    USER = 0,
    KERNEL,
    TOTAL
};

bool containsOnlyDigits(const std::string& str) {
    for (char c : str) {
        if (!isdigit(c)) {
            return false;
        }
    }
    return true;
}

double readCPUUtilization(enum CPUUtilizationType type)
{
    auto proc_stat = "/proc/stat";
    std::ifstream fileStat(proc_stat);
    if (!fileStat.is_open())
    {
        const std::string error = "Failed to open /proc/stat" ;
        throw std::runtime_error(error);
    }

    std::string firstLine, labelName;
    std::size_t timeData[NUM_CPU_STATES_TIME];

    std::getline(fileStat, firstLine);
    std::stringstream ss(firstLine);
    ss >> labelName;

    if (DEBUG)
        std::cout << "CPU stats first Line is " << firstLine << "\n";

    if (labelName.compare("cpu"))
    {
        error("CPU data not available");
        return -1;
    }

    int i;
    for (i = 0; i < NUM_CPU_STATES_TIME; i++)
    {
        if (!(ss >> timeData[i]))
            break;
    }

    if (i != NUM_CPU_STATES_TIME)
    {
        error("CPU data not correct");
        return -1;
    }

    static std::unordered_map<enum CPUUtilizationType, uint64_t> preActiveTime,
        preTotalTime;

    // These are actually Jiffies. On the BMC, 1 jiffy usually corresponds to
    // 0.01 second.
    uint64_t activeTime = 0, activeTimeDiff = 0, totalTime = 0,
             totalTimeDiff = 0;
    double activePercValue = 0;

    if (type == TOTAL)
    {
        activeTime = timeData[USER_IDX] + timeData[NICE_IDX] +
                     timeData[SYSTEM_IDX] + timeData[IRQ_IDX] +
                     timeData[SOFTIRQ_IDX] + timeData[STEAL_IDX] +
                     timeData[GUEST_USER_IDX] + timeData[GUEST_NICE_IDX];
    }
    else if (type == KERNEL)
    {
        activeTime = timeData[SYSTEM_IDX];
    }
    else if (type == USER)
    {
        activeTime = timeData[USER_IDX];
    }

    totalTime = std::accumulate(std::begin(timeData), std::end(timeData), 0);

    activeTimeDiff = activeTime - preActiveTime[type];
    totalTimeDiff = totalTime - preTotalTime[type];

    /* Store current idle and active time for next calculation */
    preActiveTime[type] = activeTime;
    preTotalTime[type] = totalTime;
    if(totalTimeDiff == 0)
    {
        return std::numeric_limits<double>::quiet_NaN();
    }
    activePercValue = (100.0 * activeTimeDiff) / totalTimeDiff;

    if (DEBUG)
        std::cout << "CPU Utilization is " << activePercValue << "\n";

    return activePercValue;
}

std::any readCPUUtilizationTotal([[maybe_unused]] std::vector<std::any> args)
{
    return readCPUUtilization(CPUUtilizationType::TOTAL);
}

std::any readCPUUtilizationKernel([[maybe_unused]] std::vector<std::any> args)
{
    return readCPUUtilization(CPUUtilizationType::KERNEL);
}

std::any readCPUUtilizationUser([[maybe_unused]] std::vector<std::any> args)
{
    return readCPUUtilization(CPUUtilizationType::USER);
}

std::any readMemoryUtilization([[maybe_unused]] std::vector<std::any> args)
{
    /* Unused var: path */
    std::ignore = args;
    std::ifstream meminfo("/proc/meminfo");
    if(!meminfo.is_open())
    {
        const std::string error = "Failed to open. /proc/meminfo";
        throw std::runtime_error(error);
    }
    std::string line;
    double memTotal = -1;
    double memAvail = -1;

    while (std::getline(meminfo, line))
    {
        std::string name;
        double value;
        std::istringstream iss(line);

        if (!(iss >> name >> value))
        {
            continue;
        }

        if (name.starts_with("MemTotal"))
        {
            memTotal = value;
        }
        else if (name.starts_with("MemAvailable"))
        {
            memAvail = value;
        }
    }

    if (memTotal <= 0 || memAvail <= 0)
    {
        return std::numeric_limits<double>::quiet_NaN();
    }

    if (DEBUG)
    {
        std::cout << "MemTotal: " << memTotal << " MemAvailable: " << memAvail
                  << std::endl;
    }

    return (memTotal - memAvail) / memTotal * 100;
}

std::any readStorageUtilization([[maybe_unused]] std::vector<std::any> args)
{
    if(args.size() != 1 || args[0].type() != typeid(std::string))
    {
        throw std::invalid_argument("Invalid argument");
    }
    
    std::string path = std::any_cast<std::string>(args[0]);
    struct statvfs buffer 
    {};
    int ret = statvfs(path.c_str(), &buffer);
    double total = 0;
    double available = 0;
    double used = 0;
    double usedPercentage = 0;

    if (ret != 0)
    {
        auto e = errno;
        std::cerr << "Error from statvfs: " << strerror(e) << ",path: " << path
                  << std::endl;
        return 0;
    }

    total = buffer.f_blocks * (buffer.f_frsize / 1024);
    available = buffer.f_bfree * (buffer.f_frsize / 1024);
    used = total - available;
    if(total == 0)
    {
        return std::numeric_limits<double>::quiet_NaN();
    }
    usedPercentage = (used / total) * 100;

    if (DEBUG)
    {
        std::cout << "Total:" << total << "\n";
        std::cout << "Available:" << available << "\n";
        std::cout << "Used:" << used << "\n";
        std::cout << "Storage utilization is:" << usedPercentage << "\n";
    }

    return usedPercentage;
}

std::any readInodeUtilization([[maybe_unused]] std::vector<std::any> args)
{
    if(args.size() != 1 || args[0].type() != typeid(std::string))
    {
        throw std::invalid_argument("Invalid argument");
    }

    std::string path = std::any_cast<std::string>(args[0]);
    struct statvfs buffer
    {};
    int ret = statvfs(path.c_str(), &buffer);
    double totalInodes = 0;
    double availableInodes = 0;
    double used = 0;
    double usedPercentage = 0;

    if (ret != 0)
    {
        auto e = errno;
        std::cerr << "Error from statvfs: " << strerror(e) << ",path: " << path
                  << std::endl;
        return 0;
    }

    totalInodes = buffer.f_files;
    availableInodes = buffer.f_ffree;
    used = totalInodes - availableInodes;
    if(totalInodes == 0)
    {
        return std::numeric_limits<double>::quiet_NaN();
    }
    usedPercentage = (used / totalInodes) * 100;

    if (DEBUG)
    {
        std::cout << "Total Inodes:" << totalInodes << "\n";
        std::cout << "Available Inodes:" << availableInodes << "\n";
        std::cout << "Used:" << used << "\n";
        std::cout << "Inodes utilization is:" << usedPercentage << "\n";
    }

    return usedPercentage;
}

int getSystemClockFrequency()
{
    // get the number of clock ticks per second
    int hertz = sysconf(_SC_CLK_TCK);
    if(hertz <= 0)
    {
        hertz = 100; // assuming normal linux system
    }
    info("hertz is {HERTZ}", "HERTZ", hertz);
    return hertz;
}

int getNumberofCPU()
{
    // get the number of processors in system
    int cpus = sysconf(_SC_NPROCESSORS_ONLN);
    if(cpus <= 0)
    {
        cpus = 1;
    }
    info("cpus is {CPUS}", "CPUS", cpus);
    return cpus;
}

double readProcessCPUUtilization(int pid)
{
    std::string statFilePath = "/proc/" + std::to_string(pid) + "/stat";
    std::ifstream statFile(statFilePath);
    if (!statFile.is_open()) 
    {
        const std::string error = "Failed to open. " + statFilePath;
        throw std::runtime_error(error);
    }

    // Read the line from the stat file
    std::string line;
    std::getline(statFile, line);
    statFile.close();

    // Parse the line to extract the required fields
    std::istringstream iss(line);
    std::string comm, state;
    int ppid, pgrp, session, tty_nr, tpgid, flags, minflt, cminflt, majflt,
        cmajflt;

    int utime, stime, cutime, cstime;
    long priority, nice, num_threads, itrealvalue;
    int starttime;

    // Extract the required fields from the line
    iss >> pid >> comm >> state >> ppid >> pgrp >> session >> tty_nr >> tpgid >>
        flags >> minflt >> cminflt >> majflt >> cmajflt >> utime >> stime >>
        cutime >> cstime >> priority >> nice >> num_threads >> itrealvalue >>
        starttime;

    // Calculate the total time spent by the process in user mode and kernel
    // mode
    int activeTime = utime + stime;// + cutime + cstime;
    if(DEBUG)
    {
        info("activeTime is {ACTIVE_TIME}", "ACTIVE_TIME", activeTime);
    }

    struct timeval t;
    struct timezone timez;
    float elapsedTime;

    static std::unordered_map<int, std::pair<__time_t, __suseconds_t> > preElapsedTime;
    static std::unordered_map<int, int> preActiveTime;
    gettimeofday(&t, &timez);

    if(preElapsedTime.find(pid) == preElapsedTime.end() ||
                preActiveTime.find(pid) == preActiveTime.end())
    {
        preElapsedTime[pid] = std::make_pair(0, 0);
        preActiveTime[pid] = 0;
    }

    elapsedTime = (t.tv_sec - preElapsedTime[pid].first)
	+ (float) (t.tv_usec - preElapsedTime[pid].second) / 1000000.0;

    preElapsedTime[pid] = std::make_pair(t.tv_sec, t.tv_usec);
    if(DEBUG)
    {
        info("elapsedTime is {ELAPSED_TIME}", "ELAPSED_TIME", elapsedTime);
    }

    int activeTimeDiff = activeTime - preActiveTime[pid];
    if(DEBUG)
    {
        info("activeTimeDiff is {ACTIVE_TIME_DIFF}", "ACTIVE_TIME_DIFF", activeTimeDiff);
    }
    preActiveTime[pid] = activeTime;

    if(DEBUG)
    {
        info("hertz is {HERTZ}", "HERTZ", hertz);
        info("cpus is {CPUS}", "CPUS", cpus);
    }

    if(elapsedTime <= 0 || hertz <= 0 || cpus <= 0)
    {
        return std::numeric_limits<double>::quiet_NaN();
    }

    // Calculate the CPU usage percentage
    float cpuUsagePercentage =  ( activeTimeDiff/cpus ) * ( 100/hertz ) / elapsedTime;
    if(DEBUG)
    {
        info("CPU percentage for process {PID} is {CPU_PERCENTAGE}", "PID", pid,
            "CPU_PERCENTAGE", cpuUsagePercentage);
    }

    if(cpuUsagePercentage > 100)
    {
        cpuUsagePercentage = 100;
    }
    return (double)cpuUsagePercentage;
}

std::any readServiceCpuUtilization([[maybe_unused]] std::vector<std::any> args)
{
    if(args.size() != 1 || args[0].type() != typeid(int))
    {
        return std::numeric_limits<double>::quiet_NaN();
    }
    
    int pid = std::any_cast<int>(args[0]);

    return readProcessCPUUtilization(pid);
}

// Function to calculate memory usage in kilobytes
long long calculateMemoryUsageKB(const int& pid) {
    // Build the path to the statm file for the specified process ID
    std::string statmPath = "/proc/" + std::to_string(pid) + "/statm";
    std::ifstream statmFile(statmPath);
    // Open the statm file for reading
    if (!statmFile.is_open()) 
    {
        const std::string error = "Failed to open. " + statmPath;
        throw std::runtime_error(error);
    }
    // Read the resident set size (RSS) from the statm file
    long long resident;
    statmFile >> resident;

    // Calculate memory usage in kilobytes (KB)
    long long memoryUsageKB = resident * sysconf(_SC_PAGESIZE) / 1024;
    statmFile.close();
    if(DEBUG)
    {
        info("Memory usage for process {PID} is {MEMORY_USAGE}", "PID", pid,
            "MEMORY_USAGE", memoryUsageKB);
    }
    return memoryUsageKB;
}

// Function to calculate the total memory on the system in kilobytes
long long calculateTotalMemoryKB() {
    // Build the path to the meminfo file
    std::string meminfoPath = "/proc/meminfo";
    std::ifstream meminfoFile(meminfoPath);
    
    // Open the meminfo file for reading
    if (!meminfoFile.is_open())
    {
        const std::string error = "Failed to open. " + meminfoPath;
        throw std::runtime_error(error);
    }

    // Read the MemTotal field from meminfo file
    std::string line;
    long long totalMemoryKB = -1;
    while (std::getline(meminfoFile, line)) {
        if (line.find("MemTotal:") == 0) {
            std::istringstream iss(line);
            std::string field;
            iss >> field >> totalMemoryKB;
            break;
        }
    }
    meminfoFile.close();
    if(DEBUG)
    {
        info("Total memory on the system is {TOTAL_MEMORY}", "TOTAL_MEMORY", totalMemoryKB);
    }
    return totalMemoryKB;
}

std::any readServiceMemoryUtilization([[maybe_unused]] [[maybe_unused]] std::vector<std::any> args)
{
    if(args.size() != 1 || args[0].type() != typeid(int))
    {
        throw std::invalid_argument("Invalid argument");
    }

    int pid = std::any_cast<int>(args[0]);

    long long memoryUsageKB = calculateMemoryUsageKB(pid);
    long long totalMemoryKB = calculateTotalMemoryKB();

    if (memoryUsageKB != -1 && totalMemoryKB != -1 && totalMemoryKB > 0) 
    {
        double memoryUsagePercentage = static_cast<double>(memoryUsageKB) / totalMemoryKB * 100.0;
        if(DEBUG)
        {
            info("Memory percentage for process {PID} is {MEMORY_PERCENTAGE}", "PID", pid,
                "MEMORY_PERCENTAGE", memoryUsagePercentage);
        }
        return memoryUsagePercentage;
    }
    else
    {
        return std::numeric_limits<double>::quiet_NaN();
    }
}

constexpr auto storage = "Storage";
constexpr auto inode = "Inode";
constexpr auto serviceCPU = "Service_CPU_";
constexpr auto serviceMemory = "Service_Memory_";
constexpr auto serviceCPUOther = "Other_CPU_";
constexpr auto serviceMemoryOther = "Other_Memory_";

/** Map of read function for each health sensors supported
 *
 * The following health sensors are read in the ManagerDiagnosticData
 * Redfish resource:
 *  - CPU_Kernel populates ProcessorStatistics.KernelPercent
 *  - CPU_User populates ProcessorStatistics.UserPercent
 */
using GenericFunction = std::function<std::any(std::vector<std::any>)>;
std::map<std::string, GenericFunction>
    readSensors = {{"CPU", readCPUUtilizationTotal},
                   {"CPU_Kernel", readCPUUtilizationKernel},
                   {"CPU_User", readCPUUtilizationUser},
                   {"Memory", readMemoryUtilization},
                   {storage, readStorageUtilization},
                   {inode, readInodeUtilization},
                   {serviceCPU, readServiceCpuUtilization},
                   {serviceMemory, readServiceMemoryUtilization}};

void HealthSensor::setSensorThreshold(double criticalHigh, double warningHigh)
{
    CriticalInterface::criticalHigh(criticalHigh);
    CriticalInterface::criticalLow(std::numeric_limits<double>::quiet_NaN());

    WarningInterface::warningHigh(warningHigh);
    WarningInterface::warningLow(std::numeric_limits<double>::quiet_NaN());
}

void HealthSensor::setSensorValueToDbus(const double value)
{
    ValueIface::value(value);
}

void HealthSensor::initHealthSensor(
    const std::vector<std::string>& bmcInventoryPaths)
{
    info("{SENSOR} Health Sensor initialized", "SENSOR", sensorConfig.name);

    /* Look for sensor read functions and Read Sensor values */
    auto it = readSensors.find(sensorConfig.name);

    if (sensorConfig.name.rfind(storage, 0) == 0)
    {
        it = readSensors.find(storage);
    }
    else if (sensorConfig.name.rfind(inode, 0) == 0)
    {
        it = readSensors.find(inode);
    }
    else if(sensorConfig.name.rfind(serviceCPU, 0) == 0)
    {
        it = readSensors.find(serviceCPU);
    }
    else if (sensorConfig.name.rfind(serviceMemory, 0) == 0)
    {
        it = readSensors.find(serviceMemory);
    }
    else if (it == readSensors.end())
    {
        error("Sensor read function not available");
        return;
    }

    double value = -1;
    try
    {
        if ((it->first == serviceCPU) || (it->first == serviceMemory))
        {
            std::vector<std::any> args = {pid};
            std::any result = it->second(args);
            value = std::any_cast<double>(result);
        }
        else
        {
            std::vector<std::any> args = {sensorConfig.path};
            std::any result = it->second(args);
            value = std::any_cast<double>(result);
        }
    }
    catch(const std::exception& e)
    {
        error("Exception occurred while reading sensor {SENSOR}: {ERROR}",
              "SENSOR", sensorConfig.name, "ERROR", e);
        readTimer.setEnabled(false);
    }

    if (value < 0)
    {
        error(": {SENSOR}", "SENSOR",
              sensorConfig.name);
        return;
    }

    /* Initialize unit value (Percent) for utilization sensor */
    ValueIface::unit(ValueIface::Unit::Percent);

    ValueIface::maxValue(100);
    ValueIface::minValue(0);
    ValueIface::value(std::numeric_limits<double>::quiet_NaN());

    // Associate the sensor to chassis
    // This connects the DBus object to a Chassis.

    std::vector<AssociationTuple> associationTuples;
    for (const auto& chassisId : bmcInventoryPaths)
    {
        // This utilization sensor "is monitoring" the BMC with path chassisId.
        // The chassisId is "monitored_by" this utilization sensor.
        associationTuples.push_back({"monitors", "monitored_by", chassisId});
    }
    AssociationDefinitionInterface::associations(associationTuples);

    /* Start the timer for reading sensor data at regular interval */
    readTimer.restart(std::chrono::milliseconds(sensorConfig.freq * 1000));
}
void HealthSensor::createRFLogEntry(const std::string& messageId,
                                    const std::string& messageArgs,
                                    const std::string& level,
                                    const std::string& resolution)
{
    auto method = this->bus.new_method_call(
        "xyz.openbmc_project.Logging", "/xyz/openbmc_project/logging",
        "xyz.openbmc_project.Logging.Create", "Create");
    // Signature is ssa{ss}
    method.append(messageId);
    method.append(level);
    method.append(std::array<std::pair<std::string, std::string>, 3>(
        {std::pair<std::string, std::string>({"REDFISH_MESSAGE_ID", messageId}),
         std::pair<std::string, std::string>(
             {"REDFISH_MESSAGE_ARGS", messageArgs}),
              std::pair<std::string, std::string>({"xyz.openbmc_project.Logging.Entry.Resolution", resolution})}));
    try
    {
        // A strict timeout for logging service to fail early and ensure
        // the original caller does not encounter dbus timeout
        uint64_t timeout_us = 10000000;

        this->bus.call_noreply(method, timeout_us);
    }
    catch (const sdbusplus::exception::exception& e)
    {
        error("Failed to create log entry, exception:{ERROR}", "ERROR", e);
    }
}

void HealthSensor::createThresholdLogEntry(const std::string& threshold,
                                          const std::string& sensorName,
                                          double value,
                                          const double configThresholdValue)
{

    std::string messageId = "OpenBMC.0.4.";
    std::string messageArgs{};
    std::string messageLevel{};
    std::string resolution{};
    if (threshold == "warning")
    {

        messageId += "SensorThresholdWarningHighGoingHigh";
        messageArgs = sensorName + "," + std::to_string(value) + "," +
                      std::to_string(configThresholdValue);
        messageLevel = "xyz.openbmc_project.Logging.Entry.Level.Warning";
        resolution = "None";
        createRFLogEntry(messageId, messageArgs, messageLevel, resolution);
    }
    else if (threshold == "critical")
    {
        messageId += "SensorThresholdCriticalHighGoingHigh";
        messageArgs = sensorName + "," + std::to_string(value) + "," +
                      std::to_string(configThresholdValue);
        messageLevel = "xyz.openbmc_project.Logging.Entry.Level.Critical";
        resolution ="None";
        createRFLogEntry(messageId, messageArgs, messageLevel, resolution);
    }
    else
    {
        error("ERROR: Invalid threshold {TRESHOLD} used for log creation ",
              "TRESHOLD", threshold);
    }
}

bool HealthSensor::checkCriticalLogRateLimitWindow()
{
    if (!std::chrono::duration_cast<std::chrono::seconds>(
                lastCriticalLogLoggedTime.time_since_epoch())
            .count())
    {
        // Update the last Critical log loggedTime
        lastCriticalLogLoggedTime =
            std::chrono::high_resolution_clock::now();
        return true;
    }
    std::chrono::duration<double> diff =
        std::chrono::high_resolution_clock::now() -
        lastCriticalLogLoggedTime;
    if (diff.count() > healthMon->getlogRateLimit())
    {
        // Update the last Critical log loggedTime
        lastCriticalLogLoggedTime =
            std::chrono::high_resolution_clock::now();
        return true;
    }
    return false;
}

bool HealthSensor::checkWarningLogRateLimitWindow()
{
    if (!std::chrono::duration_cast<std::chrono::seconds>(
                lastWarningLogLoggedTime.time_since_epoch())
            .count())
    {
        // Update the last warning log loggedTime
        lastWarningLogLoggedTime =
            std::chrono::high_resolution_clock::now();
        return true;
    }
    std::chrono::duration<double> diff =
        std::chrono::high_resolution_clock::now() -
        lastWarningLogLoggedTime;

    if (diff.count() > healthMon->getlogRateLimit())
    {
        // Update the last warning log loggedTime
        lastWarningLogLoggedTime =
            std::chrono::high_resolution_clock::now();
        return true;
    }
    return false;
}

bool HealthSensor::checkPeakLogRateLimitWindow()
{
    if (!std::chrono::duration_cast<std::chrono::seconds>(
                lastPeakLogLoggedTime.time_since_epoch())
            .count())
    {
        // Update the last Peak log loggedTime
        lastPeakLogLoggedTime =
            std::chrono::high_resolution_clock::now();
        return true;
    }
    std::chrono::duration<double> diff =
        std::chrono::high_resolution_clock::now() -
        lastPeakLogLoggedTime;

    if (diff.count() > healthMon->getlogRateLimit())
    {
        // Update the last Peak log loggedTime
        lastPeakLogLoggedTime =
            std::chrono::high_resolution_clock::now();
        return true;
    }
    return false;
}
void HealthSensor::checkSensorThreshold(const double value)
{
    std::string path;
    if (std::isfinite(sensorConfig.criticalHigh) &&
        (value > sensorConfig.criticalHigh))
    {
        if (!CriticalInterface::criticalAlarmHigh())
        {
            CriticalInterface::criticalAlarmHigh(true);
            if (sensorConfig.criticalLog)
            {
                error(
                    "ASSERT: sensor {SENSOR} is above the upper threshold critical "
                    "high",
                    "SENSOR", sensorConfig.name);
                if (checkCriticalLogRateLimitWindow())
                {
                    createThresholdLogEntry("critical", sensorConfig.name, value,
                                           sensorConfig.criticalHigh);

                    if ((sensorConfig.name.rfind(serviceCPU, 0) == 0))
                    {
                         startUnit(sensorConfig.criticalTgt,
                                "CPU", sensorConfig.binaryName, value);

                    }
                    else if(sensorConfig.name.rfind(serviceMemory, 0) == 0)
                    {
                         startUnit(sensorConfig.criticalTgt,
                                "Memory", sensorConfig.binaryName, value);
                    }
                    else
                    {
                        if (sensorConfig.name.rfind(storage, 0) == 0)
                        {
                            path = sensorConfig.path;
                        }
                        startUnit(sensorConfig.criticalTgt,
                                sensorConfig.name, path);
                    }
                }
            }
        }
        return;
    }

    if (CriticalInterface::criticalAlarmHigh())
    {
        CriticalInterface::criticalAlarmHigh(false);
        if (sensorConfig.criticalLog)
            info(
                "DEASSERT: sensor {SENSOR} is under the upper threshold critical high",
                "SENSOR", sensorConfig.name);
    }

    if (std::isfinite(sensorConfig.warningHigh) &&
        (value > sensorConfig.warningHigh))
    {
        if (!WarningInterface::warningAlarmHigh())
        {
            WarningInterface::warningAlarmHigh(true);
            if (sensorConfig.warningLog)
            {
                error(
                    "ASSERT: sensor {SENSOR} is above the upper threshold warning high",
                    "SENSOR", sensorConfig.name);
                if (checkWarningLogRateLimitWindow())
                {
                    createThresholdLogEntry("warning", sensorConfig.name, value,
                                           sensorConfig.warningHigh);
                    if ((sensorConfig.name.rfind(serviceCPU, 0) == 0))
                    {
                         startUnit(sensorConfig.warningTgt,
                                "CPU", sensorConfig.binaryName, value);

                    }
                    else if(sensorConfig.name.rfind(serviceMemory, 0) == 0)
                    {
                         startUnit(sensorConfig.warningTgt,
                                "Memory", sensorConfig.binaryName, value);
                    }
                    else
                    {
                        if (sensorConfig.name.rfind(storage, 0) == 0)
                        {
                            path = sensorConfig.path;
                        }
                        startUnit(sensorConfig.warningTgt,
                                sensorConfig.name, path);
                    }
                }
            }
        }
        return;
    }

    if (WarningInterface::warningAlarmHigh())
    {
        WarningInterface::warningAlarmHigh(false);
        if (sensorConfig.warningLog)
            info(
                "DEASSERT: sensor {SENSOR} is under the upper threshold warning high",
                "SENSOR", sensorConfig.name);
    }
}

void HealthSensor::checkServiceCpuPeak(const double value)
{
    if (!(sensorConfig.name.rfind(serviceCPU, 0) == 0))
    {
        return;
    }

    if(value > sensorConfig.criticalHigh)
    {
        if(checkPeakLogRateLimitWindow())
        {
            info(
                "ASSERT: sensor {SENSOR} peak is above the upper threshold critical high",
                "SENSOR", sensorConfig.name);
            std::string messageId = "OpenBMC.0.4.";
            std::string messageArgs{};
            std::string messageLevel{};
            std::string resolution{};
            messageId += "BMCServicePeakResourceInfo";
            messageArgs = sensorConfig.name + "," + std::to_string(value) + "," +
                      std::to_string(sensorConfig.criticalHigh);
            messageLevel = "xyz.openbmc_project.Logging.Entry.Level.Critical";
            resolution ="None";
            createRFLogEntry(messageId, messageArgs, messageLevel, resolution);            
        }
    }
}
void HealthSensor::readHealthSensor()
{
    /* Read current sensor value */
    double value = 0;
    try
    {
        if (sensorConfig.name.rfind(storage, 0) == 0)
        {
            std::vector<std::any> args = {sensorConfig.path};
            std::any result = readSensors.find(storage)->second(args);
            value = std::any_cast<double>(result);
        }
        else if (sensorConfig.name.rfind(inode, 0) == 0)
        {
            std::vector<std::any> args = {sensorConfig.path};
            std::any result = readSensors.find(inode)->second(args);
            value = std::any_cast<double>(result);
        }
        else if (sensorConfig.name.rfind(serviceCPU, 0) == 0)
        {
            std::vector<std::any> args = {pid};
            std::any result = readSensors.find(serviceCPU)->second(args);
            value = std::any_cast<double>(result);
        }
        else if (sensorConfig.name.rfind(serviceMemory, 0) == 0)
        {
            std::vector<std::any> args = {pid};
            std::any result = readSensors.find(serviceMemory)->second(args);
            value = std::any_cast<double>(result);
        }
        else
        {   std::vector<std::any> args = {sensorConfig.path};
            std::any result = readSensors.find(sensorConfig.name)->second(args);
            value = std::any_cast<double>(result);
        }
    }
    catch(const std::exception& e)
    {
        error("Exception occurred while reading sensor {SENSOR}: {ERROR}",
              "SENSOR", sensorConfig.name, "ERROR", e);
        readTimer.setEnabled(false);
    }
    if (value < 0)
    {
        error("Reading Sensor Utilization failed: {SENSOR}", "SENSOR",
              sensorConfig.name);
        return;
    }

    /* Remove first item from the queue */
    if (valQueue.size() >= sensorConfig.windowSize)
    {
        valQueue.pop_front();
    }
    /* Add new item at the back */
    valQueue.push_back(value);
    
    /*check if the sensor reading has crossed peak*/
    checkServiceCpuPeak(value);

    /* Wait until the queue is filled with enough reference*/
    if (valQueue.size() < sensorConfig.windowSize)
    {
        return;
    }

    /* Calculate average values for the given window size */
    double avgValue = 0;
    avgValue = accumulate(valQueue.begin(), valQueue.end(), avgValue);
    avgValue = avgValue / sensorConfig.windowSize;

    /* Set this new value to dbus */
    setSensorValueToDbus(avgValue);

    /* Check the sensor threshold  and log required message */
    checkSensorThreshold(avgValue);
}

void HealthSensor::startUnit(const std::string& sysdUnit,
                             const std::string& resource,
                             const std::string& path)
{
    if (sysdUnit.empty())
    {
        return;
    }
    auto service = sysdUnit;
    std::string args;
    args += "\\x20";
    args += resource;
    args += "\\x20";
    args += path;

    std::replace(args.begin(), args.end(), '/', '-');
    auto p = service.find('@');
    if (p != std::string::npos)
        service.insert(p + 1, args);

    sdbusplus::message_t msg = bus.new_method_call(
        "org.freedesktop.systemd1", "/org/freedesktop/systemd1",
        "org.freedesktop.systemd1.Manager", "StartUnit");
    msg.append(service, "replace");
    bus.call_noreply(msg);
}

void HealthSensor::startUnit(const std::string& sysdUnit,
                             const std::string& resource,
                             const std::string& binaryname,
                             const double usage)
{
    if (sysdUnit.empty())
    {
        return;
    }
    auto service = sysdUnit;
    std::string args;
    args += "\\x20";
    args += resource;
    args += "\\x20";
    args += binaryname;
    args += "\\x20";
    args += usage;

    std::replace(args.begin(), args.end(), '/', '-');
    auto p = service.find('@');
    if (p != std::string::npos)
        service.insert(p + 1, args);

    sdbusplus::message_t msg = bus.new_method_call(
        "org.freedesktop.systemd1", "/org/freedesktop/systemd1",
        "org.freedesktop.systemd1.Manager", "StartUnit");
    msg.append(service, "replace");
    bus.call_noreply(msg);
}

void HealthMon::recreateSensors()
{
    PHOSPHOR_LOG2_USING;
    healthSensors.clear();

    // Find BMC inventory paths and create health sensors
    std::vector<std::string> bmcInventoryPaths =
        findPathsWithType(bus, BMC_INVENTORY_ITEM);
    createHealthSensors(bmcInventoryPaths);
    createServiceHealthSensor(bmcInventoryPaths);
}

void printConfig(HealthConfig& cfg)
{
#ifdef PRINT_HELTH_CONFIG_ENABLED
    std::cout << "Name: " << cfg.name << "\n";
    std::cout << "Freq: " << (int)cfg.freq << "\n";
    std::cout << "Window Size: " << (int)cfg.windowSize << "\n";
    std::cout << "Critical value: " << (int)cfg.criticalHigh << "\n";
    std::cout << "warning value: " << (int)cfg.warningHigh << "\n";
    std::cout << "Critical log: " << (int)cfg.criticalLog << "\n";
    std::cout << "Warning log: " << (int)cfg.warningLog << "\n";
    std::cout << "Critical Target: " << cfg.criticalTgt << "\n";
    std::cout << "Warning Target: " << cfg.warningTgt << "\n\n";
    std::cout << "Path : " << cfg.path << "\n\n";
#endif
}

/* Create dbus utilization sensor object */
void HealthMon::createHealthSensors(
    const std::vector<std::string>& bmcInventoryPaths)
{
    for (auto& cfg : sensorConfigs)
    {
        std::string objPath = std::string(HEALTH_SENSOR_PATH) + cfg.name;
        auto healthSensor = std::make_shared<HealthSensor>(
            bus, objPath.c_str(), cfg, bmcInventoryPaths);
        healthSensors.emplace(cfg.name, healthSensor);

        info("{SENSOR} Health Sensor created", "SENSOR", cfg.name);

        /* Set configured values of crtical and warning high to dbus */
        healthSensor->setSensorThreshold(cfg.criticalHigh, cfg.warningHigh);
    }
}

void HealthMon::createServiceHealthSensorInstance(HealthConfig& cfg, 
                            const std::vector<std::string>& bmcInventoryPaths,
                            int pid)
{
    std::string objPath = std::string(HEALTH_SENSOR_PATH) + cfg.name;

    auto healthSensor = std::make_shared<HealthSensor>(
            bus, objPath.c_str(), cfg, bmcInventoryPaths, pid);
    healthSensors.emplace(cfg.name, healthSensor);

    info("{SENSOR} Health Sensor created", "SENSOR", cfg.name);

    /* Set configured values of crtical and warning high to dbus */
    healthSensor->setSensorThreshold(cfg.criticalHigh, cfg.warningHigh);
}

/* Create dbus utilization sensor object for each configured sensors */
void HealthMon::createServiceHealthSensor(
    const std::vector<std::string>& bmcInventoryPaths)
{
    DIR* dir = opendir("/proc");
    if (!dir) {
        error("Failed to open the /proc directory");
        return; 
    }
    dirent* entry;
    while ((entry = readdir(dir))) {
        if (entry->d_type == DT_DIR) {
            // Check if the directory name is a number (potential process ID)
            if (!containsOnlyDigits(entry->d_name)) {
                continue;
            }
            int pid = std::stoi(entry->d_name);
            if (pid != 0) 
            {
                // Build the path to the "comm" file for this process
                std::string commPath = "/proc/" + std::to_string(pid) + "/comm";

                // Open the "comm" file for reading
                std::ifstream commFile(commPath);
                if (commFile) 
                {
                    std::string processName;
                    std::getline(commFile, processName);

                    // Check if the process name is pre-configured
                    for (auto& cfg : serviceSensorConfigs)
                    {
                        if (cfg.binaryName == processName && ((cfg.name.rfind(serviceMemory, 0) == 0 ) 
                                                          || (cfg.name.rfind(serviceCPU, 0) == 0 )))
                        {
                            createServiceHealthSensorInstance(cfg, bmcInventoryPaths, pid);
                        }
                    }
                    //TODO: handle dynamic services and multiple process instances here
                }
            }
        }
    }
    closedir(dir);
}

/** @brief Parsing Health config JSON file  */
Json HealthMon::parseConfigFile(std::string configFile)
{
    std::ifstream jsonFile(configFile);
    if (!jsonFile.is_open())
    {
        error("config JSON file not found: {PATH}", "PATH", configFile);
    }

    auto data = Json::parse(jsonFile, nullptr, false);
    if (data.is_discarded())
    {
        error("config readings JSON parser failure: {PATH}", "PATH",
              configFile);
    }

    return data;
}

void HealthMon::getConfigData(Json& data, HealthConfig& cfg)
{
    static const Json empty{};

    /* Default frerquency of sensor polling is 1 second */
    cfg.freq = data.value("Frequency", 1);

    /* Default window size sensor queue is 1 */
    cfg.windowSize = data.value("Window_size", 1);

    auto threshold = data.value("Threshold", empty);
    if (!threshold.empty())
    {
        auto criticalData = threshold.value("Critical", empty);
        if (!criticalData.empty())
        {
            cfg.criticalHigh =
                criticalData.value("Value", defaultHighThreshold);
            cfg.criticalLog = criticalData.value("Log", true);
            cfg.criticalTgt = criticalData.value("Target", "");
        }
        auto warningData = threshold.value("Warning", empty);
        if (!warningData.empty())
        {
            cfg.warningHigh = warningData.value("Value", defaultHighThreshold);
            cfg.warningLog = warningData.value("Log", true);
            cfg.warningTgt = warningData.value("Target", "");
        }
    }
    cfg.path = data.value("Path", "");
    cfg.binaryName = data.value("BinaryName", "");
}

std::vector<HealthConfig> HealthMon::getHealthConfig()
{
    std::vector<HealthConfig> cfgs;
    auto data = parseConfigFile(HEALTH_CONFIG_FILE);

    // print values
    if (DEBUG)
    {
        std::cout << "Config json data:\n" << data << "\n\n";
    }
    /* Get data items from config json data*/
    for (auto& j : data.items())
    {
        auto key = j.key();
        if (key == "LogRateLimit")
        {
            logRateLimit = j.value();
            continue;
        }

        if (key == "BootDelay")
        {
            bootDelay = j.value();
            continue;
        }

        /* key need match default value in map readSensors or match the key
         * start with "Storage" or "Inode" */
        bool isStorageOrInode =
            (key.rfind(storage, 0) == 0 || key.rfind(inode, 0) == 0);
        if (readSensors.find(key) != readSensors.end() || isStorageOrInode)
        {
            HealthConfig cfg = HealthConfig();
            cfg.name = j.key();
            getConfigData(j.value(), cfg);
            if (isStorageOrInode)
            {
                struct statvfs buffer
                {};
                int ret = statvfs(cfg.path.c_str(), &buffer);
                if (ret != 0)
                {
                    auto e = errno;
                    std::cerr << "Error from statvfs: " << strerror(e)
                              << ", name: " << cfg.name
                              << ", path: " << cfg.path
                              << ", please check your settings in config file."
                              << std::endl;
                    continue;
                }
            }
            cfgs.push_back(cfg);
            if (DEBUG)
                printConfig(cfg);
        }
        else
        {
            error("{SENSOR} Health Sensor not supported", "SENSOR", key);
        }
    }
    return cfgs;
}

std::vector<HealthConfig> HealthMon::getServiceHealthConfig()
{
    std::vector<HealthConfig> cfgs;
    auto data = parseConfigFile(SERVICE_HEALTH_CONFIG_FILE);
    // print values
    if (DEBUG)
        std::cout << "Config json data:\n" << data << "\n\n";
    /* Get data items from config json data*/
    for (auto& j : data.items())
    {
        auto key = j.key();
        /* key need match default value in map readSensors or match the key
         * start with "Service_CPU_" or "Service_Memory_" */
        bool isServiceCPUOrMemory =
            (key.rfind(serviceCPU, 0) == 0 || key.rfind(serviceMemory, 0) == 0);
        if (readSensors.find(key) != readSensors.end() || isServiceCPUOrMemory)
        {
            HealthConfig cfg = HealthConfig();
            cfg.name = j.key();
            getConfigData(j.value(), cfg);
            cfgs.push_back(cfg);
            if (DEBUG)
                printConfig(cfg);
        }
        else
        {
            error("{SENSOR} Health Sensor not supported", "SENSOR", key);
        }
    }
    return cfgs;
}

// Two caveats here.
// 1. The BMC Inventory will only show up by the nearest ObjectMapper polling
// interval.
// 2. InterfacesAdded events will are not emitted like they are with E-M.
void HealthMon::createBmcInventoryIfNotCreated()
{
    if (bmcInventory == nullptr)
    {
        info("createBmcInventory");
        bmcInventory = std::make_shared<phosphor::health::BmcInventory>(
            bus, "/xyz/openbmc_project/inventory/bmc");
    }
}

bool HealthMon::bmcInventoryCreated()
{
    return bmcInventory != nullptr;
}

void HealthMon::sleepuntilSystemBoot()
{
    info("health monitor is waiting for system boot");
    // wait for the system to boot up
    sleep(bootDelay);
}

} // namespace health
} // namespace phosphor

void sensorRecreateTimerCallback(
    std::shared_ptr<boost::asio::steady_timer> timer, sdbusplus::bus_t& bus)
{
    timer->expires_after(std::chrono::seconds(TIMER_INTERVAL));
    timer->async_wait([timer, &bus](const boost::system::error_code& ec) {
        if (ec == boost::asio::error::operation_aborted)
        {
            info("sensorRecreateTimer aborted");
            return;
        }

        // When Entity-manager is already running
        if (!needUpdate)
        {
            if ((!healthMon->bmcInventoryCreated()) &&
                (!phosphor::health::findPathsWithType(bus, BMC_CONFIGURATION)
                      .empty()))
            {
                healthMon->createBmcInventoryIfNotCreated();
                needUpdate = true;
            }
        }
        else
        {
            // If this daemon maintains its own DBus object, we must make sure
            // the object is registered to ObjectMapper
            if (phosphor::health::findPathsWithType(bus, BMC_INVENTORY_ITEM)
                    .empty())
            {
                info(
                    "BMC inventory item not registered to Object Mapper yet, waiting for next iteration");
            }
            else
            {
                info(
                    "BMC inventory item registered to Object Mapper, creating sensors now");
                healthMon->recreateSensors();
                needUpdate = false;
            }
        }
        sensorRecreateTimerCallback(timer, bus);
    });
}

/**
 * @brief Main
 */
int main()
{
    // The io_context is needed for the timer
    boost::asio::io_context io;

    // DBus connection
    auto conn = std::make_shared<sdbusplus::asio::connection>(io);

    conn->request_name(HEALTH_BUS_NAME);

    // Get a default event loop
    auto event = sdeventplus::Event::get_default();

    // number of cpus
    cpus = phosphor::health::getNumberofCPU();

    // Get the number of clock ticks per second
    hertz = phosphor::health::getSystemClockFrequency();

    // Create an health monitor object
    healthMon = std::make_shared<phosphor::health::HealthMon>(*conn);

    // Sleep until system boot
    healthMon->sleepuntilSystemBoot();

    // Create sensors, since the system is booted up
    // This is needed since some processes are not started until the system is booted
    // up, e.g. oobamld gppmgrd
    // This case will be handled when dynamic services are supported
    // and below line can be moved to .hpp file as it was before.
    healthMon->recreateSensors();

    // Add object manager through object_server
    sdbusplus::asio::object_server objectServer(conn);

    sdbusplus::asio::sd_event_wrapper sdEvents(io);

    sensorRecreateTimer = std::make_shared<boost::asio::steady_timer>(io);

    // If the SystemInventory does not exist: wait for the InterfaceAdded signal
    auto interfacesAddedSignalHandler = std::make_unique<
        sdbusplus::bus::match_t>(
        static_cast<sdbusplus::bus_t&>(*conn),
        sdbusplus::bus::match::rules::interfacesAdded(),
        [conn](sdbusplus::message_t& msg) {
            using Association =
                std::tuple<std::string, std::string, std::string>;
            using InterfacesAdded = std::vector<std::pair<
                std::string,
                std::vector<std::pair<
                    std::string, std::variant<std::vector<Association>>>>>>;

            sdbusplus::message::object_path o;
            InterfacesAdded interfacesAdded;

            try
            {
                msg.read(o);
                msg.read(interfacesAdded);
            }
            catch (const std::exception& e)
            {
                error(
                    "Exception occurred while processing interfacesAdded:  {EXCEPTION}",
                    "EXCEPTION", e.what());
                return;
            }

            // Ignore any signal coming from health-monitor itself.
            if (msg.get_sender() == conn->get_unique_name())
            {
                return;
            }

            // Check if the BMC Inventory is in the interfaces created.
            bool hasBmcConfiguration = false;
            for (const auto& x : interfacesAdded)
            {
                if (x.first == BMC_CONFIGURATION)
                {
                    hasBmcConfiguration = true;
                }
            }

            if (hasBmcConfiguration)
            {
                info(
                    "BMC configuration detected, will create a corresponding Inventory item");
                healthMon->createBmcInventoryIfNotCreated();
                needUpdate = true;
            }
        });

    // Start the timer
    boost::asio::post(io, [conn]() {
        sensorRecreateTimerCallback(sensorRecreateTimer, *conn);
    });
    io.run();

    return 0;
}
