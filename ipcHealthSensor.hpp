#pragma once
#include "ipcConfig.hpp"

#include <boost/asio/steady_timer.hpp>
#include <boost/container/flat_map.hpp>
#include <sdbusplus/bus.hpp>
#include <sdbusplus/message.hpp>
#include <sdeventplus/clock.hpp>
#include <sdeventplus/event.hpp>
#include <sdeventplus/utility/timer.hpp>

#include <chrono>
#include <deque>
#include <limits>
#include <map>
#include <string>
#include <variant>
namespace phosphor
{
namespace ipc
{
/** @class IPCHealthSensor
 *  @brief IPC sensor Base class to read sensor data
 */
class IPCHealthSensor
{
  public:
    // Forcing explicit construction and deletion
    IPCHealthSensor() = delete;
    IPCHealthSensor(const IPCHealthSensor&) = delete;
    IPCHealthSensor& operator=(const IPCHealthSensor&) = delete;
    IPCHealthSensor(IPCHealthSensor&&) = delete;
    IPCHealthSensor& operator=(IPCHealthSensor&&) = delete;
    virtual ~IPCHealthSensor();

    /** @brief Constructs IPCHealthSensor
     *
     * @param[in] bus     - Handle to system dbus
     * @param[in] objPath - The Dbus path of health sensor
     */
    IPCHealthSensor(sdbusplus::bus_t& bus, IPCConfig& ipcConfig,
                    boost::asio::io_context& io) :
        bus(bus),
        ipcConfig(ipcConfig), timer(io)
    {}
    /** @brief Initialize sensor, set default value and association */
    void initSensor();
    static std::shared_ptr<IPCHealthSensor>
        getIPCHealthSensor(sdbusplus::bus_t& bus, IPCConfig& ipcConfig,
                           boost::asio::io_context& io);

    /** @brief Check Sensor threshold and create log  and take action*/
    virtual void checkSensorThreshold(const double value,
                                      const std::string& serviceName,
                                      struct ParamConfig cfg);

    /** @brief create Sensor Treshold Redfish log  */
    void createThresholdLogEntry(const std::string& threshold,
                                 const std::string& sensorName,
                                 const std::string& proprtyName, double value,
                                 const double configThresholdValue);
    void createRFLogEntry(const std::string& messageId,
                          const std::string& messageArgs,
                          const std::string& level);
    /** @brief Read sensor data */
    void readSensordata();

  protected:
    // map to store the sensor value and its queue
    boost::container::flat_map<std::pair<std::string, std::string>,
                               std::deque<std::variant<long int, double>>>
        valQueueMap;

    // map to store the IPC sensor and its string value
    boost::container::flat_map<std::pair<std::string, std::string>, std::string>
        valStringMap;

    // map to store the IPC sensor log status
    boost::container::flat_map<std::pair<std::string, std::string>,
                               std::pair<bool, bool>>
        logStatusMap;

    /** the statistcis to get from sensor */
    std::vector<std::pair<std::string, std::string>> statistics;
    /** response of IPC call stored as class member to avoid copy elison */
    std::map<std::string,
             std::vector<std::pair<
                 std::string, std::variant<long int, double, std::string>>>>
        asyncResp;
    /** @brief sdbusplus bus client connection. */
    sdbusplus::bus_t& bus;
    /** @brief Sensor config from config file */
    IPCConfig& ipcConfig;
    /** @brief Timer to read sensor at regular interval */
    boost::asio::steady_timer timer;

    std::chrono::time_point<std::chrono::high_resolution_clock>
        lastCriticalLogLoggedTime;
    std::chrono::time_point<std::chrono::high_resolution_clock>
        lastWarningLogLoggedTime;
    /** @brief Read sensor at regular intrval */
    virtual void readSensor() = 0;
    /** @brief Initialize IPC sensor */
    virtual void init() = 0;
    /** @brief check critical log rate limit */
    bool checkCriticalLogRateLimitWindow();
    /** @brief check warning log rate limit */
    bool checkWarningLogRateLimitWindow();
    /** @brief Start configured threshold systemd unit */
    void startUnit(const std::string& sysdUnit, const std::string& serviceName,
                   const std::string& additionalData);
};

} // namespace ipc
} // namespace phosphor
