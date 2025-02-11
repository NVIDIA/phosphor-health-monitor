#pragma once

#include <sdbusplus/message.hpp>
#include <xyz/openbmc_project/Common/Threshold/server.hpp>

#include <chrono>
#include <limits>
#include <map>
#include <string>
#include <vector>

namespace phosphor::health::metric
{

using ThresholdIntf =
    sdbusplus::server::xyz::openbmc_project::common::Threshold;

enum class Type
{
    cpu,
    memory,
    storage,
    inode,
    emmc,
    processCPU,
    processMemory,
    unknown
};

enum class SubType
{
    // CPU subtypes
    cpuKernel,
    cpuTotal,
    cpuUser,
    // Memory subtypes
    memoryAvailable,
    memoryBufferedAndCached,
    memoryFree,
    memoryShared,
    memoryTotal,
    cpuProcesses,
    memoryProcesses,
    // EMMC subtypes
    emmcLifetime,
    emmcBlocks,
    // Types for which subtype is not applicable
    NA
};

auto to_string(Type) -> std::string;
auto to_string(SubType) -> std::string;

namespace config
{

using namespace std::literals::chrono_literals;

struct Threshold
{
    double value = defaults::value;
    bool log = false;
    std::string target = defaults::target;

    using map_t =
        std::map<std::tuple<ThresholdIntf::Type, ThresholdIntf::Bound>,
                 Threshold>;

    struct defaults
    {
        static constexpr auto value = std::numeric_limits<double>::quiet_NaN();
        static constexpr auto target = "";
    };
};

struct HealthMetric
{
    /** @brief The name of the metric. */
    std::string name = "unnamed";
    /** @brief The binary name of the metric. */
    std::string binaryName = "unnamed";
    /** @brief The frequency of the metric. */
    uint16_t frequency = defaults::frequency;
    /** @brief The metric subtype. */
    SubType subType = SubType::NA;
    /** @brief The window size for the metric. */
    size_t windowSize = defaults::windowSize;
    /** @brief The hysteresis for the metric */
    double hysteresis = defaults::hysteresis;
    /** @brief The threshold configs for the metric. */
    Threshold::map_t thresholds{};
    /** @brief The path for filesystem metric */
    std::string path = defaults::path;

    using map_t = std::map<Type, std::vector<HealthMetric>>;

    struct defaults
    {
        static constexpr auto windowSize = 12;
        static constexpr auto path = "";
        static constexpr auto hysteresis = 1.0;
        static constexpr auto frequency = 1;
    };
};

/** @brief Get the health metric configs. */
auto getHealthMetricConfigs() -> HealthMetric::map_t;

/** @brief Get the Service metric configs. */
auto getServiceMetricConfigs() -> HealthMetric::map_t;

} // namespace config
} // namespace phosphor::health::metric
