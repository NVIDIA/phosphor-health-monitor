#include "health_metric_collection.hpp"

#include <phosphor-logging/lg2.hpp>

#include <fstream>
#include <numeric>
#include <unordered_map>
#include <string>
#include <dirent.h>
#include <cstring>
#include <fstream>
#include <sstream>
extern "C"
{
#include <sys/statvfs.h>
}

PHOSPHOR_LOG2_USING;

namespace phosphor::health::metric::collection
{

int HealthMetricCollection::hertz = phosphor::health::utils::getSystemClockFrequency();
int HealthMetricCollection::cpus = phosphor::health::utils::getNumberofCPU();

auto HealthMetricCollection::readProcessCPU() -> bool
{
    for (auto& config : configs)
    {
        if(metrics.find(config.name) == metrics.end())
        {
            continue;
        }
        debug("Reading process CPU metric for {NAME}", "NAME", config.name);
        int pid = metrics[config.name]->getPid();
        if(pid <= -1)
        {
            error("Failed to get PID for process {NAME}", "NAME", config.name);
            continue;
        }
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
        debug("activeTime is {ACTIVE_TIME}", "ACTIVE_TIME", activeTime);

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
        debug("elapsedTime is {ELAPSED_TIME}", "ELAPSED_TIME", elapsedTime);

        int activeTimeDiff = activeTime - preActiveTime[pid];
        debug("activeTimeDiff is {ACTIVE_TIME_DIFF}", "ACTIVE_TIME_DIFF", activeTimeDiff);
        preActiveTime[pid] = activeTime;

        debug("hertz is {HERTZ}", "HERTZ", hertz);
        debug("cpus is {CPUS}", "CPUS", cpus);

        if(elapsedTime <= 0 || hertz <= 0 || cpus <= 0)
        {
            continue;
        }

        // Calculate the CPU usage percentage
        double  cpuUsagePercentage =  ( activeTimeDiff/cpus ) * ( 100/hertz ) / elapsedTime;
        debug("CPU percentage for process {PID} is {CPU_PERCENTAGE}", "PID", pid,
            "CPU_PERCENTAGE", cpuUsagePercentage);

        if(cpuUsagePercentage > 100)
        {
            cpuUsagePercentage = 100;
        }
        /* Update percentage values */
        metrics[config.name]->update(MValue(cpuUsagePercentage, 100.0));
    }
    return true;
}

// Function to calculate the total memory on the system in kilobytes
long long HealthMetricCollection::calculateTotalMemory()
{
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
    debug("Total memory on the system is {TOTAL_MEMORY}", "TOTAL_MEMORY", totalMemoryKB);
    return totalMemoryKB;
}
auto HealthMetricCollection::readProcessMemory() -> bool
{
    long long totalMemoryKB = calculateTotalMemory();

    for (auto& config : configs)
    {
        if(metrics.find(config.name) == metrics.end())
        {
            continue;
        }
        int pid = metrics[config.name]->getPid();
        if(pid <= 0)
        {
            error("Failed to get PID for process {NAME}", "NAME", config.name);
            continue;
        }
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

        // Calculate memory usage in kilo bytes
        long long memoryUsageKB = resident * sysconf(_SC_PAGESIZE) / 1024;

        double memoryUsagePercentage = static_cast<double>(memoryUsageKB) / totalMemoryKB * 100.0;
        statmFile.close();

        debug("Memory percentage for process {PID} is {MEMORY_PERCENTAGE}", "PID", pid,
            "MEMORY_PERCENTAGE", memoryUsagePercentage);
        metrics[config.name]->update(MValue(memoryUsagePercentage, 100));
    }
    return true;
}

auto HealthMetricCollection::readCPU() -> bool
{
    enum CPUStatsIndex
    {
        userIndex = 0,
        niceIndex,
        systemIndex,
        idleIndex,
        iowaitIndex,
        irqIndex,
        softirqIndex,
        stealIndex,
        guestUserIndex,
        guestNiceIndex,
        maxIndex
    };
    constexpr auto procStat = "/proc/stat";
    std::ifstream fileStat(procStat);
    if (!fileStat.is_open())
    {
        error("Unable to open {PATH} for reading CPU stats", "PATH", procStat);
        return false;
    }

    std::string firstLine, labelName;
    std::size_t timeData[CPUStatsIndex::maxIndex] = {0};

    std::getline(fileStat, firstLine);
    std::stringstream ss(firstLine);
    ss >> labelName;

    if (labelName.compare("cpu"))
    {
        error("CPU data not available");
        return false;
    }

    for (auto idx = 0; idx < CPUStatsIndex::maxIndex; idx++)
    {
        if (!(ss >> timeData[idx]))
        {
            error("CPU data not correct");
            return false;
        }
    }

    for (auto& config : configs)
    {
        uint64_t activeTime = 0, activeTimeDiff = 0, totalTime = 0,
                 totalTimeDiff = 0;
        double activePercValue = 0;

        if (config.subType == MetricIntf::SubType::cpuTotal)
        {
            activeTime = timeData[CPUStatsIndex::userIndex] +
                         timeData[CPUStatsIndex::niceIndex] +
                         timeData[CPUStatsIndex::systemIndex] +
                         timeData[CPUStatsIndex::irqIndex] +
                         timeData[CPUStatsIndex::softirqIndex] +
                         timeData[CPUStatsIndex::stealIndex] +
                         timeData[CPUStatsIndex::guestUserIndex] +
                         timeData[CPUStatsIndex::guestNiceIndex];
        }
        else if (config.subType == MetricIntf::SubType::cpuKernel)
        {
            activeTime = timeData[CPUStatsIndex::systemIndex];
        }
        else if (config.subType == MetricIntf::SubType::cpuUser)
        {
            activeTime = timeData[CPUStatsIndex::userIndex];
        }

        totalTime = std::accumulate(std::begin(timeData), std::end(timeData),
                                    decltype(totalTime){0});

        activeTimeDiff = activeTime - preActiveTime[config.subType];
        totalTimeDiff = totalTime - preTotalTime[config.subType];

        /* Store current active and total time for next calculation */
        preActiveTime[config.subType] = activeTime;
        preTotalTime[config.subType] = totalTime;

        activePercValue = (100.0 * activeTimeDiff) / totalTimeDiff;
        debug("CPU Metric {SUBTYPE}: {VALUE}", "SUBTYPE", config.subType,
              "VALUE", (double)activePercValue);
        /* For CPU, both user and monitor uses percentage values */
        metrics[config.name]->update(MValue(activePercValue, 100));
    }
    return true;
}

auto HealthMetricCollection::readMemory() -> bool
{
    constexpr auto procMeminfo = "/proc/meminfo";
    std::ifstream memInfo(procMeminfo);
    if (!memInfo.is_open())
    {
        error("Unable to open {PATH} for reading Memory stats", "PATH",
              procMeminfo);
        return false;
    }
    std::string line;
    std::unordered_map<MetricIntf::SubType, double> memoryValues;

    while (std::getline(memInfo, line))
    {
        std::string name;
        double value;
        std::istringstream iss(line);

        if (!(iss >> name >> value))
        {
            continue;
        }
        if (name.starts_with("MemAvailable"))
        {
            memoryValues[MetricIntf::SubType::memoryAvailable] = value;
        }
        else if (name.starts_with("MemFree"))
        {
            memoryValues[MetricIntf::SubType::memoryFree] = value;
        }
        else if (name.starts_with("Buffers") || name.starts_with("Cached"))
        {
            memoryValues[MetricIntf::SubType::memoryBufferedAndCached] += value;
        }
        else if (name.starts_with("MemTotal"))
        {
            memoryValues[MetricIntf::SubType::memoryTotal] = value;
        }
        else if (name.starts_with("Shmem"))
        {
            memoryValues[MetricIntf::SubType::memoryShared] += value;
        }
    }

    for (auto& config : configs)
    {
        // Convert kB to Bytes
        auto value = memoryValues.at(config.subType) * 1024;
        auto total = memoryValues.at(MetricIntf::SubType::memoryTotal) * 1024;
        debug("Memory Metric {SUBTYPE}: {VALUE}, {TOTAL}", "SUBTYPE",
              config.subType, "VALUE", value, "TOTAL", total);
        metrics[config.name]->update(MValue(value, total));
    }
    return true;
}

auto HealthMetricCollection::readStorage() -> bool
{
    for (auto& config : configs)
    {
        struct statvfs buffer;
        debug("Reading storage metric for {PATH}", "PATH", config.path);
        if (statvfs(config.path.c_str(), &buffer) != 0)
        {
            auto e = errno;
            error("Error from statvfs: {ERROR}, path: {PATH}", "ERROR",
                  strerror(e), "PATH", config.path);
            continue;
        }
        double value = buffer.f_bfree * buffer.f_frsize;
        double total = buffer.f_blocks * buffer.f_frsize;
        debug("Storage Metric {SUBTYPE}: {VALUE}, {TOTAL}", "SUBTYPE",
              config.subType, "VALUE", value, "TOTAL", total);
        metrics[config.name]->update(MValue(value, total));
    }
    return true;
}

void HealthMetricCollection::read()
{
    switch (type)
    {
        case MetricIntf::Type::cpu:
        {
            if (!readCPU())
            {
                error("Failed to read CPU health metric");
            }
            break;
        }
        case MetricIntf::Type::memory:
        {
            if (!readMemory())
            {
                error("Failed to read memory health metric");
            }
            break;
        }
        case MetricIntf::Type::storage:
        {
            if (!readStorage())
            {
                error("Failed to read storage health metric");
            }
            break;
        }
        case MetricIntf::Type::processCPU:
        {
            try
            {
                readProcessCPU();
            }
            catch (const std::exception& e)
            {
                error("Failed to read process CPU health metric: {ERROR}", "ERROR", e.what());
            }
            break;
        }
        case MetricIntf::Type::processMemory:
        {
            try
            {
                readProcessMemory();
            }
            catch (const std::exception& e)
            {
                error("Failed to read process memory health metric: {ERROR}", "ERROR", e.what());
            }
            break;
        }
        default:
        {
            error("Unknown health metric type {TYPE}", "TYPE", type);
            break;
        }
    }
}

void HealthMetricCollection::create(const MetricIntf::paths_t& bmcPaths)
{
    metrics.clear();
    if(type == MetricIntf::Type::processCPU || type == MetricIntf::Type::processMemory)
    {
        createProcessMetric(bmcPaths);
    }
    else
    {
        for (auto& config : configs)
        {
            debug("Creating health metric {NAME}", "NAME", config.name);
            metrics[config.name] = std::make_unique<MetricIntf::HealthMetric>(
                bus, type, config, bmcPaths);
        }
    }
}

void HealthMetricCollection::createProcessMetric(const MetricIntf::paths_t& bmcPaths)
{
     DIR* dir = opendir("/proc");
    if (!dir) {
        error("Failed to open the /proc directory");
        return; 
    }
    dirent* entry;
    while ((entry = readdir(dir)))
    {
        if (entry->d_type == DT_DIR)
        {
            // Check if the directory name is a number (potential process ID)
            if (! phosphor::health::utils::containsOnlyDigits(entry->d_name)) {
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

                    for (auto config : configs)
                    {
                        if (config.binaryName == processName)
                        {
                            // Create a new health metric object for this process
                            debug("Creating health metric for process {NAME}", "NAME", config.name);
                            metrics[config.name] = std::make_unique<MetricIntf::HealthMetric>(
                                    bus, type, config, bmcPaths, pid);
                        }
                    }
                }
            }
        }
    }
    closedir(dir);
}

} // namespace phosphor::health::metric::collection
