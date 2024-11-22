#include "config.h"

#include "health_monitor.hpp"

#include "health_metric.hpp"

#include <phosphor-logging/lg2.hpp>
#include <sdbusplus/async.hpp>
#include <xyz/openbmc_project/Inventory/Item/Bmc/common.hpp>
#include <xyz/openbmc_project/Inventory/Item/common.hpp>

PHOSPHOR_LOG2_USING;

namespace phosphor::health::monitor
{
using namespace phosphor::health::utils;

auto HealthMonitor::startup() -> sdbusplus::async::task<>
{
    info("Creating Health Monitor with config size {SIZE}", "SIZE",
         configs.size());

    static constexpr auto bmcIntf = sdbusplus::common::xyz::openbmc_project::
        inventory::item::Bmc::interface;
    static constexpr auto invPath = sdbusplus::common::xyz::openbmc_project::
        inventory::Item::namespace_path;
    auto bmcPaths = co_await findPaths(ctx, bmcIntf, invPath);

    for (auto& [type, collectionConfig] : configs)
    {
        info("Creating Health Metric Collection for {TYPE}", "TYPE", type);
        collections[type] =
            std::make_unique<CollectionIntf::HealthMetricCollection>(
                ctx.get_bus(), type, collectionConfig, bmcPaths);
    }

    co_await run();
}

auto HealthMonitor::run() -> sdbusplus::async::task<>
{
    info("Running Health Monitor");
    while (!ctx.stop_requested())
    {
        for (auto& [type, collection] : collections)
        {
            debug("Reading Health Metric Collection for {TYPE}", "TYPE", type);
            collection->read();
        }
        // Checking for Pending Metrics
        for (auto& [type, collection] : collections)
        {
            if (collection->getPendingConfigsCount() > 0)
            {
                debug("Pending Metrics found for {TYPE}", "TYPE", type);
                collection->createPendingConfigs();
            }
        }
        co_await sdbusplus::async::sleep_for(
            ctx, std::chrono::seconds(MONITOR_COLLECTION_INTERVAL));
    }
}
} // namespace phosphor::health::monitor

using namespace phosphor::health::monitor;

int main()
{
    constexpr auto path = MetricIntf::ValueIntf::Value::namespace_path::value;
    sdbusplus::async::context ctx;
    sdbusplus::server::manager_t manager{ctx, path};
    constexpr auto healthMonitorServiceName = "xyz.openbmc_project.HealthMon";
    phosphor::health::metric::HealthMetric::setBootTime(
        std::chrono::high_resolution_clock::now());
    info("Creating health monitor");
    using namespace phosphor::health::metric::config;
    // parseCommonConfig();
    std::function<HealthMetric::map_t()> healthConfigFunc =
        getHealthMetricConfigs;
    HealthMonitor healthMonitor(ctx, healthConfigFunc);

    std::function<HealthMetric::map_t()> srvcConfigFunction =
        getServiceMetricConfigs;
    HealthMonitor serviceMonitor(ctx, srvcConfigFunction);

    ctx.request_name(healthMonitorServiceName);

    ctx.run();
    return 0;
}
