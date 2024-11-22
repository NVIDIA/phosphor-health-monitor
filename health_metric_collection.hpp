#pragma once

#include "health_metric.hpp"

namespace phosphor::health::metric::collection
{
namespace ConfigIntf = phosphor::health::metric::config;
namespace MetricIntf = phosphor::health::metric;

using configs_t = std::vector<ConfigIntf::HealthMetric>;

class HealthMetricCollection
{
  public:
    HealthMetricCollection(sdbusplus::bus_t& bus, MetricIntf::Type type,
                           const configs_t& configs,
                           MetricIntf::paths_t& bmcPaths) :
        bus(bus),
        type(type), configs(configs), bmcPaths(bmcPaths)
    {
        create(bmcPaths);
    }

    /** @brief Read the health metric collection from the system */
    void read();
    /** @brief Get the number of pending metrics */
    int getPendingConfigsCount()
    {
        return pendingConfigs.size();
    }
    /** @brief Add the pending metric */
    void addPendingConfig(const std::string configName)
    {
        pendingConfigs.insert(configName);
    }

    /** @brief Remove the pending metric */
    void removePendingConfig(std::string& configName)
    {
        pendingConfigs.erase(configName);
    }
    /** @brief Create the pending metrics */
    void createPendingConfigs();

  private:
    using map_t = std::unordered_map<std::string,
                                     std::unique_ptr<MetricIntf::HealthMetric>>;
    using time_map_t = std::unordered_map<MetricIntf::SubType, uint64_t>;
    /** @brief Create a new health metric collection object */
    void create(const MetricIntf::paths_t& bmcPaths);
    /** @brief Create the health metric collection object for process cpu/memory
     * type */
    void createProcessMetric(const MetricIntf::paths_t& bmcPaths);
    /** @brief Read the CPU */
    auto readCPU() -> bool;
    /** @brief Read the memory */
    auto readMemory() -> bool;
    /** @brief Read the storage */
    auto readStorage() -> bool;
    /** @brief Read the eMMC health */
    auto readEMMC() -> bool;
    /** @brief read process cpu usage*/
    auto readProcessCPU() -> bool;
    /** @brief read process memory usage*/
    auto readProcessMemory() -> bool;
    /** @brief Calculate the total memory in KB */
    long long calculateTotalMemory();
    /** @brief D-Bus bus connection */
    sdbusplus::bus_t& bus;
    /** @brief Metric type */
    MetricIntf::Type type;
    /** @brief Health metric configs */
    const configs_t& configs;
    /** @brief Map of health metrics by subtype */
    map_t metrics;
    /** @brief Map for active time by subtype */
    time_map_t preActiveTime;
    /** @brief Map for total time by subtype */
    time_map_t preTotalTime;
    /** total number cpus*/
    static int cpus;
    /** @brief clock ticks per second */
    static int hertz;
    /** @brief data structure for storing pending Metrics*/
    std::set<std::string> pendingConfigs;

    MetricIntf::paths_t bmcPaths;
};

} // namespace phosphor::health::metric::collection
