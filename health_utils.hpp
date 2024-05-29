#pragma once

#include <sdbusplus/async.hpp>
#include <sdbusplus/bus.hpp>
#include <sdbusplus/sdbus.hpp>
#include <xyz/openbmc_project/Common/Threshold/server.hpp>

#include <vector>
namespace phosphor::health::utils
{

using paths_t = std::vector<std::string>;
using namespace sdbusplus::common::xyz::openbmc_project::common;
/** @brief Start a systemd unit */
void startUnit(sdbusplus::bus_t& bus, const std::string& sysdUnit,
               const std::string resource, const std::string path = "",
               const std::string binaryname = "", const double usage = 0.0);

/** @brief Find D-Bus paths for given interface */
auto findPaths(sdbusplus::async::context& ctx, const std::string& iface,
               const std::string& subpath) -> sdbusplus::async::task<paths_t>;

void createThresholdLogEntry(sdbusplus::bus_t& bus, Threshold::Type& type,
                             Threshold::Bound& bound,
                             const std::string& sensorName, double value,
                             const double configThresholdValue);

/* Create a log entry in the RFLog */
void createRFLogEntry(sdbusplus::bus_t& bus, const std::string& messageId,
                      const std::string& messageArgs, const std::string& level,
                      const std::string& resolution);

/** @brief Check if a string contains only digits */
bool containsOnlyDigits(const std::string& str);

/** @brief Get the system clock frequency */
int getSystemClockFrequency();

/** @brief Get the number of CPUs */
int getNumberofCPU();
} // namespace phosphor::health::utils
