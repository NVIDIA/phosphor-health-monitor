#include "config.h"

#include "health_metric.hpp"

#include <phosphor-logging/lg2.hpp>

#include <cmath>
#include <numeric>
#include <unordered_map>

PHOSPHOR_LOG2_USING;

namespace phosphor::health::metric
{

using association_t = std::tuple<std::string, std::string, std::string>;

auto HealthMetric::getPath(MType type, std::string name,
                           SubType subType) -> std::string
{
    std::string path;
    switch (subType)
    {
        case SubType::cpuTotal:
        {
            return std::string(BmcPath) + "/" + PathIntf::total_cpu;
        }
        case SubType::cpuKernel:
        {
            return std::string(BmcPath) + "/" + PathIntf::kernel_cpu;
        }
        case SubType::cpuUser:
        {
            return std::string(BmcPath) + "/" + PathIntf::user_cpu;
        }
        case SubType::memoryAvailable:
        {
            return std::string(BmcPath) + "/" + PathIntf::available_memory;
        }
        case SubType::memoryBufferedAndCached:
        {
            return std::string(BmcPath) + "/" +
                   PathIntf::buffered_and_cached_memory;
        }
        case SubType::memoryFree:
        {
            return std::string(BmcPath) + "/" + PathIntf::free_memory;
        }
        case SubType::memoryShared:
        {
            return std::string(BmcPath) + "/" + PathIntf::shared_memory;
        }
        case SubType::memoryTotal:
        {
            return std::string(BmcPath) + "/" + PathIntf::total_memory;
        }
        case SubType::cpuProcesses:
        {
            static constexpr auto nameDelimiter = "_";
            auto processName = name.substr(name.find_last_of(nameDelimiter) + 1,
                                           name.length());
            std::ranges::for_each(processName, [](auto& c) {
                c = std::tolower(c);
            });
            return std::string(BmcPath) + "/" + "cpu/processes" + "/" +
                   processName;
        }
        case SubType::memoryProcesses:
        {
            static constexpr auto nameDelimiter = "_";
            auto processName = name.substr(name.find_last_of(nameDelimiter) + 1,
                                           name.length());
            std::ranges::for_each(processName, [](auto& c) {
                c = std::tolower(c);
            });
            return std::string(BmcPath) + "/" + "memory/processes" + "/" +
                   processName;
        }
        case SubType::emmcLifetime:
        {
            return std::string(BmcPath) + "/" + PathIntf::emmc_lifetime;
        }
        case SubType::emmcBlocks:
        {
            return std::string(BmcPath) + "/" + PathIntf::emmc_blocks;
        }
        case SubType::NA:
        {
            if (type == MType::storage)
            {
                static constexpr auto nameDelimiter = "_";
                auto storageType = name.substr(
                    name.find_last_of(nameDelimiter) + 1, name.length());
                std::ranges::for_each(storageType, [](auto& c) {
                    c = std::tolower(c);
                });
                return std::string(BmcPath) + "/" + PathIntf::storage + "/" +
                       storageType;
            }
            else
            {
                error("Invalid metric {SUBTYPE} for metric {TYPE}", "SUBTYPE",
                      subType, "TYPE", type);
                return "";
            }
        }
        default:
        {
            error("Invalid metric {SUBTYPE}", "SUBTYPE", subType);
            return "";
        }
    }
}

void HealthMetric::initProperties()
{
    switch (type)
    {
        case MType::cpu:
        case MType::emmc:
        case MType::processMemory:
        case MType::processCPU:
        {
            ValueIntf::unit(ValueIntf::Unit::Percent, true);
            ValueIntf::minValue(0.0, true);
            ValueIntf::maxValue(100.0, true);
            break;
        }
        case MType::memory:
        case MType::storage:
        {
            ValueIntf::unit(ValueIntf::Unit::Bytes, true);
            ValueIntf::minValue(0.0, true);
            break;
        }
        case MType::inode:
        case MType::unknown:
        default:
        {
            throw std::invalid_argument("Invalid metric type");
        }
    }
    ValueIntf::value(std::numeric_limits<double>::quiet_NaN(), true);

    using bound_map_t = std::map<Bound, double>;
    std::map<Type, bound_map_t> thresholds;
    for (const auto& [key, value] : config.thresholds)
    {
        auto type = std::get<Type>(key);
        auto bound = std::get<Bound>(key);
        auto threshold = thresholds.find(type);
        if (threshold == thresholds.end())
        {
            bound_map_t bounds;
            bounds.emplace(bound, std::numeric_limits<double>::quiet_NaN());
            thresholds.emplace(type, bounds);
        }
        else
        {
            threshold->second.emplace(bound, value.value);
        }
    }
    ThresholdIntf::value(thresholds, true);
}

bool didThresholdViolate(ThresholdIntf::Bound bound, double thresholdValue,
                         double value)
{
    switch (bound)
    {
        case ThresholdIntf::Bound::Lower:
        {
            return (value < thresholdValue);
        }
        case ThresholdIntf::Bound::Upper:
        {
            return (value > thresholdValue);
        }
        default:
        {
            error("Invalid threshold bound {BOUND}", "BOUND", bound);
            return false;
        }
    }
}

void HealthMetric::checkThreshold(Type type, Bound bound, MValue value)
{
    auto threshold = std::make_tuple(type, bound);
    auto thresholds = ThresholdIntf::value();

    if (thresholds.contains(type) && thresholds[type].contains(bound))
    {
        auto tConfig = config.thresholds.at(threshold);
        auto thresholdValue = tConfig.value / 100 * value.total;
        thresholds[type][bound] = thresholdValue;
        ThresholdIntf::value(thresholds);
        auto assertions = ThresholdIntf::asserted();
        if (didThresholdViolate(bound, thresholdValue, value.current))
        {
            if (!assertions.contains(threshold))
            {
                assertions.insert(threshold);
                ThresholdIntf::asserted(assertions);
                ThresholdIntf::assertionChanged(type, bound, true,
                                                value.current);
                if (tConfig.log)
                {
                    error(
                        "ASSERT: Health Metric {METRIC} crossed {TYPE} upper threshold",
                        "METRIC", config.name, "TYPE", type);
                    if ((type == Threshold::Type::Critical &&
                         checkCriticalLogRateLimitWindow()) ||
                        (type == Threshold::Type::Warning &&
                         checkWarningLogRateLimitWindow()))
                    {
                        std::string path = "";
                        phosphor::health::utils::createThresholdLogEntry(
                            bus, type, bound, config.name, value.current,
                            thresholdValue);
                        if (this->type ==
                            phosphor::health::metric::Type::processCPU)
                        {
                            startUnit(bus, tConfig.target, "CPU", path,
                                      config.binaryName, value.current);
                        }
                        else if (this->type ==
                                 phosphor::health::metric::Type::processMemory)
                        {
                            startUnit(bus, tConfig.target, "Memory", path,
                                      config.binaryName, value.current);
                        }
                        else if (this->type ==
                                 phosphor::health::metric::Type::emmc)
                        {
                            startUnit(bus, tConfig.target, config.name, path,
                                      config.binaryName, value.current);
                        }
                        else
                        {
                            if (this->type ==
                                phosphor::health::metric::Type::storage)
                            {
                                path = config.path;
                                startUnit(bus, tConfig.target, "Storage", path);
                            }
                            else
                            {
                                startUnit(bus, tConfig.target, config.name);
                            }
                        }
                    }
                }
            }
            return;
        }
        else if (assertions.contains(threshold))
        {
            assertions.erase(threshold);
            ThresholdIntf::asserted(assertions);
            ThresholdIntf::assertionChanged(type, bound, false, value.current);
            if (config.thresholds.find(threshold)->second.log)
            {
                info(
                    "DEASSERT: Health Metric {METRIC} is below {TYPE} upper threshold",
                    "METRIC", config.name, "TYPE", type);
            }
        }
    }
}

void HealthMetric::checkThresholds(MValue value)
{
    if (!waitForActionDelay() && !ThresholdIntf::value().empty())
    {
        for (auto type : {Type::HardShutdown, Type::SoftShutdown,
                          Type::PerformanceLoss, Type::Critical, Type::Warning})
        {
            checkThreshold(type, Bound::Lower, value);
            checkThreshold(type, Bound::Upper, value);
        }
    }
}

auto HealthMetric::shouldNotify(MValue value) -> bool
{
    if (std::isnan(value.current))
    {
        return true;
    }
    auto changed = std::abs(
        (value.current - lastNotifiedValue) / lastNotifiedValue * 100.0);
    if (changed >= config.hysteresis)
    {
        lastNotifiedValue = value.current;
        return true;
    }
    return false;
}

void HealthMetric::update(MValue value)
{
    ValueIntf::value(value.current, !shouldNotify(value));

    // Maintain window size for threshold calculation
    if (history.size() >= config.windowSize)
    {
        history.pop_front();
    }

    history.push_back(value.current);
    if (history.size() < config.windowSize)
    {
        // Wait for the metric to have enough samples to calculate average
        return;
    }

    double average =
        (std::accumulate(history.begin(), history.end(), 0.0)) / history.size();
    value.current = average;
#ifdef ENABLE_info
    info("Health Metric: {METRIC} average value: {VALUE}", "METRIC",
         config.name, "VALUE", value.current);
#endif
    checkThresholds(value);
}

void HealthMetric::create(const paths_t& bmcPaths)
{
    info("Create Health Metric: {METRIC}", "METRIC", config.name);
    initProperties();

    std::vector<association_t> associations;
    static constexpr auto forwardAssociation = "measuring";
    static constexpr auto reverseAssociation = "measured_by";
    for (const auto& bmcPath : bmcPaths)
    {
        /*
         * This metric is "measuring" the health for the BMC at bmcPath
         * The BMC at bmcPath is "measured_by" this metric.
         */
        associations.push_back(
            {forwardAssociation, reverseAssociation, bmcPath});
    }
    AssociationIntf::associations(associations);
}

bool HealthMetric::checkCriticalLogRateLimitWindow()
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
    // using namespace phosphor::health::metric::config;
    if (diff.count() > LOG_RATE_LIMIT)
    {
        // Update the last Critical log loggedTime
        lastCriticalLogLoggedTime = std::chrono::high_resolution_clock::now();
        return true;
    }
    return false;
}

bool HealthMetric::checkWarningLogRateLimitWindow()
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
    // using namespace phosphor::health::metric::config;
    if (diff.count() > LOG_RATE_LIMIT)
    {
        // Update the last warning log loggedTime
        lastWarningLogLoggedTime = std::chrono::high_resolution_clock::now();
        return true;
    }
    return false;
}

void HealthMetric::setwaitForActionDelay(bool value)
{
    waitForAction = value;
}

bool HealthMetric::waitForActionDelay()
{
    if (waitForAction)
    {
        std::chrono::duration<double> diff =
            std::chrono::high_resolution_clock::now() - bootTime;
        if (diff.count() > BOOT_DELAY)
        {
            waitForAction = false;
        }
    }
    return waitForAction;
}

} // namespace phosphor::health::metric
