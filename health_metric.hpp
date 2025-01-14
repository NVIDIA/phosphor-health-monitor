#pragma once

#include "health_metric_config.hpp"
#include "health_utils.hpp"

#include <xyz/openbmc_project/Association/Definitions/server.hpp>
#include <xyz/openbmc_project/Inventory/Item/Bmc/server.hpp>
#include <xyz/openbmc_project/Metric/Value/server.hpp>

#include <deque>
#include <tuple>

namespace phosphor::health::metric
{

using phosphor::health::utils::paths_t;
using phosphor::health::utils::startUnit;
using AssociationIntf =
    sdbusplus::xyz::openbmc_project::Association::server::Definitions;
using ValueIntf = sdbusplus::xyz::openbmc_project::Metric::server::Value;
using PathIntf =
    sdbusplus::common::xyz::openbmc_project::metric::Value::namespace_path;
static constexpr auto BmcPath =
    sdbusplus::common::xyz::openbmc_project::metric::Value::bmc;
using BmcIntf = sdbusplus::xyz::openbmc_project::Inventory::Item::server::Bmc;
using MetricIntf =
    sdbusplus::server::object_t<ValueIntf, ThresholdIntf, AssociationIntf>;

struct MValue
{
    /** @brief Current value of metric */
    double current;
    /** @brief Total value of metric */
    double total;
};

class HealthMetric : public MetricIntf
{
  public:
    using MType = phosphor::health::metric::Type;

    HealthMetric() = delete;
    HealthMetric(const HealthMetric&) = delete;
    HealthMetric(HealthMetric&&) = delete;
    virtual ~HealthMetric() = default;

    HealthMetric(sdbusplus::bus_t& bus, MType type,
                 const config::HealthMetric& config, const paths_t& bmcPaths) :
        MetricIntf(bus, getPath(type, config.name, config.subType).c_str(),
                   action::defer_emit),
        bus(bus), type(type), config(config)
    {
        create(bmcPaths);
        this->emit_object_added();
    }

    /** @brief Update the health metric with the given value */
    void update(MValue value);
    /** @brief Set the process ID for the metric */
    void setPid(int pid)
    {
        this->pid = pid;
    }
    /** @brief Get the process ID for the metric */
    int getPid()
    {
        return pid;
    }

    /** @brief Set the action delay flag , used by CI unit */
    static void setwaitForActionDelay(bool value);
    /*Wait for the action delay*/
    static bool waitForActionDelay();
    /** @brief Set the boot time */
    static void setBootTime(
        std::chrono::time_point<std::chrono::high_resolution_clock> time)
    {
        bootTime = time;
    }

  private:
    /** @brief Create a new health metric object */
    void create(const paths_t& bmcPaths);
    /** @brief Init properties for the health metric object */
    void initProperties();
    /** @brief Check if specified value should be notified based on hysteresis
     */
    auto shouldNotify(MValue value) -> bool;
    /** @brief Check specified threshold for the given value */
    void checkThreshold(Type type, Bound bound, MValue value);
    /** @brief Check all thresholds for the given value */
    void checkThresholds(MValue value);
    /** @brief Get the object path for the given type, name and subtype */
    auto getPath(MType type, std::string name, SubType subType) -> std::string;
    /** @brief Check if the metric is in critical state */
    auto checkCriticalLogRateLimitWindow() -> bool;
    /** @brief Check if the metric is in warning state */
    auto checkWarningLogRateLimitWindow() -> bool;
    /** @brief last logged time for critical log */
    std::chrono::time_point<std::chrono::high_resolution_clock>
        lastCriticalLogLoggedTime;
    /** @brief last logged time for warning log */
    std::chrono::time_point<std::chrono::high_resolution_clock>
        lastWarningLogLoggedTime;
    /** @brief D-Bus bus connection */
    sdbusplus::bus_t& bus;
    /** @brief Metric type */
    MType type;
    /** @brief Metric configuration */
    const config::HealthMetric config;
    /** @brief Window for metric history */
    std::deque<double> history;
    /** @brief Last notified value for the metric change */
    double lastNotifiedValue = 0;
    /** @brief Process ID for the metric */
    int pid = 0;
    /** @brief boot time */
    inline static std::chrono::time_point<std::chrono::high_resolution_clock>
        bootTime;
    /* @brief wait for action delay */
    inline static bool waitForAction = true;
};

} // namespace phosphor::health::metric
