#include "ipcConfig.hpp"
#include "ipcHealthSensor.hpp"

#include <nlohmann/json.hpp>
#include <phosphor-logging/lg2.hpp>
#include <sdbusplus/asio/connection.hpp>
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
    explicit IPCMonitor(sdbusplus::asio::connection& bus) : bus(bus)
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
    sdbusplus::asio::connection& bus;
    unsigned int logRateLimit = 0;
    std::vector<IPCConfig> configs;
    const std::vector<IPCConfig> getIPCConfig();
    unsigned int bootDelay = 0;
};

} // namespace ipc
} // namespace phosphor
