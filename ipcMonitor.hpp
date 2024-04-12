#include "ipcConfig.hpp"
#include "ipcHealthSensor.hpp"

#include <nlohmann/json.hpp>
#include <phosphor-logging/lg2.hpp>
#include <sdbusplus/bus.hpp>
#include <sdbusplus/message.hpp>
#include <sdbusplus/server/manager.hpp>
#include <sdeventplus/clock.hpp>
#include <sdeventplus/event.hpp>
#include <sdeventplus/utility/timer.hpp>

#include <string>
#include <variant>
#include <vector>
namespace phosphor
{
namespace ipc
{
using Json = nlohmann::json;
class IPCMonitor
{
  public:
    IPCMonitor() = delete;
    IPCMonitor(const IPCMonitor&) = delete;
    IPCMonitor& operator=(const IPCMonitor&) = delete;
    IPCMonitor(IPCMonitor&&) = delete;
    IPCMonitor& operator=(IPCMonitor&&) = delete;
    ~IPCMonitor() = default;

    void createSensors();
    /** @brief Constructs IPCMonitor
     *
     * @param[in] bus     - Handle to system dbus
     */
    IPCMonitor(sdbusplus::bus_t& bus, boost::asio::io_context& io) :
        bus(bus), ioc(io)
    {
        // Read JSON file
        configs = getIPCConfig();
    }

    /** Sleep until boot delay time */
    void sleepuntilSystemBoot();

    /** @brief Map of the object IPCHealthSensor */
    std::unordered_map<std::string, std::shared_ptr<IPCHealthSensor>>
        ipcSensors;

    unsigned int getlogRateLimit()
    {
        return logRateLimit;
    }

  private:
    /** @brief Logging Rate Limit */
    sdbusplus::bus_t& bus;
    boost::asio::io_context& ioc;
    unsigned int logRateLimit;
    std::vector<IPCConfig> configs;
    const std::vector<IPCConfig> getIPCConfig();
    unsigned int bootDelay;
};

} // namespace ipc
} // namespace phosphor
