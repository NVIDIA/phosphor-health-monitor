#include "config.h"

#include "health_metric_config.hpp"

#include <nlohmann/json.hpp>
#include <phosphor-logging/lg2.hpp>

#include <cmath>
#include <fstream>
#include <ranges>
#include <unordered_map>
#include <unordered_set>
#include <utility>

PHOSPHOR_LOG2_USING;

namespace phosphor::health::metric
{
namespace config
{

using json = nlohmann::json;

// Default health metric config
extern json defaultHealthMetricConfig;
extern json defaultServiceMetricConfig;

// Valid thresholds from config
static const auto validThresholdTypesWithBound =
    std::unordered_set<std::string>{"Critical_Lower", "Critical_Upper",
                                    "Warning_Lower", "Warning_Upper"};

static const auto validThresholdBounds =
    std::unordered_map<std::string, ThresholdIntf::Bound>{
        {"Lower", ThresholdIntf::Bound::Lower},
        {"Upper", ThresholdIntf::Bound::Upper}};

static const auto validThresholdTypes =
    std::unordered_map<std::string, ThresholdIntf::Type>{
        {"HardShutdown", ThresholdIntf::Type::HardShutdown},
        {"SoftShutdown", ThresholdIntf::Type::SoftShutdown},
        {"PerformanceLoss", ThresholdIntf::Type::PerformanceLoss},
        {"Critical", ThresholdIntf::Type::Critical},
        {"Warning", ThresholdIntf::Type::Warning}};

// Valid metrics from config
static const auto validTypes =
    std::unordered_map<std::string, Type>{{"CPU", Type::cpu},
                                          {"Memory", Type::memory},
                                          {"Storage", Type::storage},
                                          {"Inode", Type::inode},
                                          {"ProcessCPU", Type::processCPU},
                                          {"ProcessMemory", Type::processMemory}};

// Valid submetrics from config
static const auto validSubTypes = std::unordered_map<std::string, SubType>{
    {"CPU", SubType::cpuTotal},
    {"CPU_User", SubType::cpuUser},
    {"CPU_Kernel", SubType::cpuKernel},
    {"CPU_Processes", SubType::cpuProcesses},
    {"Memory", SubType::memoryTotal},
    {"Memory_Free", SubType::memoryFree},
    {"Memory_Available", SubType::memoryAvailable},
    {"Memory_Shared", SubType::memoryShared},
    {"Memory_Buffered_And_Cached", SubType::memoryBufferedAndCached},
    {"Memory_Processes", SubType::memoryProcesses},
    {"Storage_RW", SubType::NA},
    {"Storage_TMP", SubType::NA}};

/** Deserialize a Threshold from JSON. */
void from_json(const json& j, Threshold& self)
{
    self.value = j.value("Value", 100.0);
    self.log = j.value("Log", false);
    self.target = j.value("Target", Threshold::defaults::target);
}

/** Deserialize a HealthMetric from JSON. */
void from_json(const json& j, HealthMetric& self)
{
    self.windowSize = j.value("Window_size",
                              HealthMetric::defaults::windowSize);
    self.hysteresis = j.value("Hysteresis", HealthMetric::defaults::hysteresis);
    // Path is only valid for storage
    self.path = j.value("Path", "");
    self.binaryName = j.value("BinaryName", "");
    self.frequency = j.value("Frequency", HealthMetric::defaults::frequency);
    auto thresholds = j.find("Threshold");
    if (thresholds == j.end())
    {
        return;
    }

    for (auto& [key, value] : thresholds->items())
    {
        if (!validThresholdTypesWithBound.contains(key))
        {
            warning("Invalid ThresholdType: {TYPE}", "TYPE", key);
            continue;
        }

        auto config = value.template get<Threshold>();
        if (!std::isfinite(config.value))
        {
            throw std::invalid_argument("Invalid threshold value");
        }

        static constexpr auto keyDelimiter = "_";
        std::string typeStr = key.substr(0, key.find_first_of(keyDelimiter));
        std::string boundStr = key.substr(key.find_last_of(keyDelimiter) + 1,
                                          key.length());
        self.thresholds.emplace(
            std::make_tuple(validThresholdTypes.at(typeStr),
                            validThresholdBounds.at(boundStr)),
            config);
    }
}

json parseConfigFile(std::string configFile)
{
    std::ifstream jsonFile(configFile);
    if (!jsonFile.is_open())
    {
        info("config JSON file not found: {PATH}", "PATH", configFile);
        return {};
    }

    try
    {
        return json::parse(jsonFile, nullptr, true);
    }
    catch (const json::parse_error& e)
    {
        error("Failed to parse JSON config file {PATH}: {ERROR}", "PATH",
              configFile, "ERROR", e);
    }

    return {};
}

void printConfig(HealthMetric::map_t& configs)
{
    for (auto& [type, configList] : configs)
    {
        for (auto& config : configList)
        {
            debug(
                "TYPE={TYPE}, NAME={NAME} SUBTYPE={SUBTYPE} PATH={PATH}, WSIZE={WSIZE}, HYSTERESIS={HYSTERESIS}, BINARYNAME={BINARYNAME}, FREQUENCY={FREQUENCY}",
                "TYPE", type, "NAME", config.name, "SUBTYPE", config.subType,
                "PATH", config.path, "WSIZE", config.windowSize, "HYSTERESIS",
                config.hysteresis, "BINARYNAME", config.binaryName, "FREQUENCY", config.frequency);

            for (auto& [key, threshold] : config.thresholds)
            {
                debug(
                    "THRESHOLD TYPE={TYPE} THRESHOLD BOUND={BOUND} VALUE={VALUE} LOG={LOG} TARGET={TARGET}",
                    "TYPE", get<ThresholdIntf::Type>(key), "BOUND",
                    get<ThresholdIntf::Bound>(key), "VALUE", threshold.value,
                    "LOG", threshold.log, "TARGET", threshold.target);
            }
        }
    }
}

auto getHealthMetricConfigs() -> HealthMetric::map_t
{
    json mergedConfig(defaultHealthMetricConfig);

    if (auto platformConfig = parseConfigFile(HEALTH_CONFIG_FILE);
        !platformConfig.empty())
    {
        info("Merging platform health metric config");
        mergedConfig.merge_patch(platformConfig);
    }

    HealthMetric::map_t configs = {};
    for (auto& [name, metric] : mergedConfig.items())
    {
        static constexpr auto nameDelimiter = "_";
        std::string typeStr = name.substr(0, name.find_first_of(nameDelimiter));

        auto type = validTypes.find(typeStr);
        if (type == validTypes.end())
        {
            warning("Invalid metric type: {TYPE}", "TYPE", typeStr);
            continue;
        }

        auto config = metric.template get<HealthMetric>();
        config.name = name;

        auto subType = validSubTypes.find(name);
        config.subType = (subType != validSubTypes.end() ? subType->second
                                                         : SubType::NA);

        configs[type->second].emplace_back(std::move(config));
    }
    printConfig(configs);
    return configs;
}

json defaultHealthMetricConfig = R"({
    "CPU": {
        "Threshold": {
            "Critical_Upper": {
                "Value": 90.0,
                "Log": true,
                "Target": "HMSystemRecovery@.service"
            },
            "Warning_Upper": {
                "Value": 80.0,
                "Log": false,
                "Target": "HMSystemWarning@.service"
            }
        }
    },
    "CPU_User": {
    },
    "CPU_Kernel": {
    },
    "Memory": {
    },
    "Memory_Available": {
        "Threshold": {
            "Critical_Lower": {
                "Value": 15.0,
                "Log": true,
                "Target": "HMSystemRecovery@.service"
            }
        }
    },
    "Memory_Free": {
    },
    "Memory_Shared": {
        "Threshold": {
            "Critical_Upper": {
                "Value": 85.0,
                "Log": true,
                "Target": "HMSystemRecovery@.service"
            }
        }
    },
    "Memory_Buffered_And_Cached": {
    },
    "Storage_RW": {
        "Path": "/run/initramfs/rw",
        "Threshold": {
            "Critical_Lower": {
                "Value": 15.0,
                "Log": true,
                "Target": "HMSystemRecovery@.service"
            }
        }
    },
    "Storage_TMP": {
        "Path": "/tmp",
        "Threshold": {
            "Critical_Lower": {
                "Value": 15.0,
                "Log": true,
                "Target": "HMSystemRecovery@.service"
            }
        }
    }
})"_json;

auto getServiceMetricConfigs()  -> HealthMetric::map_t
{
    auto platformConfig = parseConfigFile(SERVICE_HEALTH_CONFIG_FILE);

    HealthMetric::map_t configs = {};
    for (auto& [name, metric] : platformConfig.items())
    {
        static constexpr auto nameDelimiter = "_";
        std::string typeStr = name.substr(0, name.find_first_of(nameDelimiter));
        auto type = validTypes.find(typeStr);
        if (type == validTypes.end())
        {
            warning("Invalid metric type: {TYPE}", "TYPE", typeStr);
            continue;
        }

        auto config = metric.template get<HealthMetric>();
        config.name = name;
        std::string subType = "NA";
        if(typeStr == "ProcessCPU")
        {
            subType = "CPU_Processes";
        }
        else if(typeStr == "ProcessMemory")
        {
            subType = "Memory_Processes";
        }
        auto var = validSubTypes.find(subType);
        config.subType = (var != validSubTypes.end() ? var->second
                                                         : SubType::NA);

        configs[type->second].emplace_back(std::move(config));
    }
    printConfig(configs);
    return configs;
}
uint16_t logRateLimit = 0;
uint16_t bootDelay = 0;
auto parseCommonConfig() -> void
{
    json platformConfig = parseConfigFile(COMMON_HEALTH_CONFIG_FILE);
    for(const auto& [key, value] : platformConfig.items())
    {
        if (key == "BootDelay") {
            bootDelay = value.get<int>();
            debug("Boot delay: {DELAY}", "DELAY", bootDelay);
        }
        if (key == "LogRateLimit") {
            logRateLimit = value.get<int>();
            debug("Log rate limit: {LIMIT}", "LIMIT", logRateLimit);
        }
    }
}

uint16_t getLogRateLimit()
{
    return logRateLimit;
}

uint16_t getBootDelay()
{
    return bootDelay;
}

} // namespace config

namespace details
{
auto reverse_map_search(const auto& m, auto v)
{
    if (auto match = std::ranges::find_if(
            m, [=](const auto& p) { return p.second == v; });
        match != std::end(m))
    {
        return match->first;
    }
    return std::format("Enum({})", std::to_underlying(v));
}
} // namespace details

// to_string specialization for Type.
auto to_string(Type t) -> std::string
{
    return details::reverse_map_search(config::validTypes, t);
}

// to_string specializaiton for SubType.
auto to_string(SubType t) -> std::string
{
    return details::reverse_map_search(config::validSubTypes, t);
}

} // namespace phosphor::health::metric
